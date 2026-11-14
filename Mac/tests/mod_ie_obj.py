"""Original synthetic IE OBJ fixtures. No external model assets are needed."""
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace

from mod_assets import mod, put


class IEObjTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mineways-ie-obj-')
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        put(self.source, 'assets/fixture/textures/white.png', mod.encode_png(1, 1, bytes((255, 255, 255, 255))))
        put(self.source, 'assets/fixture/models/test.mtl', b'newmtl surface\nKd 0 0 0\nmap_Kd fixture:white\n')
        put(self.source, 'assets/fixture/blockstates/unused.json', {'variants': {}})
        self.assets = None

    def compiler(self):
        self.assets = mod.Assets([self.source])
        self.addCleanup(self.assets.close)
        return mod.Compiler(self.assets)

    def model(self, groups, callback, **fields):
        lines = ['mtllib test.mtl', 'vt 0 0', 'vt 1 0', 'vt 0 1', 'usemtl surface']
        for i, group in enumerate(groups):
            lines += ['o ' + group, 'g ignored', f'v {i / 16} 0 0', f'v {i / 16} 1 0', f'v {i / 16} 0 1',
                      f'f {i * 3 + 1}/1 {i * 3 + 2}/2 {i * 3 + 3}/3']
        put(self.source, 'assets/fixture/models/test.obj.ie', ('\n'.join(lines) + '\n').encode())
        return {'loader': 'immersiveengineering:ie_obj', 'model': 'fixture:models/test.obj.ie',
                'callbacks': 'immersiveengineering:' + callback,
                'layers': ['solid', 'cutout', 'translucent'], **fields}

    def compile(self, groups, callback, selector=None, **fields):
        model = self.model(groups, callback, **fields)
        compiler = self.compiler()
        return compiler, compiler.compile_model({'model': model}, selector=selector)

    def test_chute_filters_alternatives_flips_v_and_ignores_mtl_color(self):
        compiler, result = self.compile(['north', 'diagonal', 'base', 'west'], 'chute')
        self.assertEqual(len(result['polygons']), 1)
        face = result['polygons'][0]
        self.assertEqual(face['vertices'], [(2, 0, 0, 0, 16), (2, 16, 0, 16, 16), (2, 0, 16, 0, 0)])
        self.assertEqual(face['texture'], 'fixture:white')
        self.assertEqual(mod.decode_png(compiler.textures[face['texture']][0])[2], bytes((255, 255, 255, 255)))
        self.assertTrue(any('default vertical base only' in warning for warning in compiler.warnings))

    def test_default_group_sets(self):
        cases = [('turret', ['base', 'gun', 'action'], [0]), ('pipe', ['center', 'curve', 'cover'], [0]),
                 ('logic_unit', ['base', 'tubes', 'board_0'], [0, 1]),
                 ('connector_rs', ['io_in', 'io_out', 'cube'], [0, 2]),
                 ('razor_wire', ['wood_left', 'wire_left', 'wood_right_inverted', 'barbs_wide'], [1, 3]),
                 ('post', ['base', 'arm_north'], [0]), ('shelf', ['frame', 'crate_0'], [0]),
                 ('workbench', ['cube0', 'blueprint'], [0]), ('cloche', ['base', 'glass', 'farmland'], [0, 1, 2]),
                 ('bottling_machine', ['base', 'glass'], [0, 1]),
                 ('chunk_loader', ['base', 'glass', 'amethyst', 'paper'], [0, 1, 2])]
        for callback, groups, expected in cases:
            with self.subTest(callback=callback):
                _, result = self.compile(groups, callback)
                self.assertEqual([p['vertices'][0][0] for p in result['polygons']], expected)

    def test_palette_lantern_and_siren_groups(self):
        groups = ['base', 'attach_t', 'attach_b', 'attach_n', 'attach_s', 'attach_w', 'attach_e']
        for side, index in zip(mod._ie_module.DIRECTIONS, range(1, 7)):
            _, result = self.compile(groups, 'lantern', {'facing': side})
            self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [0, index])
        _, result = self.compile(['main', 'base', 'extra_horn', 'wallmount'], 'siren', {'facing': 'east'})
        self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [0, 3])

    def test_lantern_expansion_preserves_resource_override(self):
        model = self.model(['base', 'attach_n'], 'lantern')
        compiler = self.compiler()
        definition = {'variants': {'': {'model': model}}}
        expanded = mod._ie_module.expand_block(compiler, 'fixture:lantern', definition, mod.ImportError)
        self.assertEqual(set(expanded['variants']), {''} | {'facing=' + d for d in mod._ie_module.DIRECTIONS})
        replacement = {'variants': {'': {'model': {'elements': []}}}}
        self.assertIs(mod._ie_module.expand_block(compiler, 'fixture:lantern', replacement, mod.ImportError), replacement)

    def test_default_dye_colors_are_group_specific_and_ignore_kd(self):
        compiler, result = self.compile(['balloon1_0', 'balloon0_1', 'base'], 'balloon')
        pixels = [mod.decode_png(compiler.textures[p['texture']][0])[2] for p in result['polygons']]
        self.assertEqual(pixels, [bytes((137, 50, 184, 255)), bytes((29, 29, 33, 255)), bytes((255, 255, 255, 255))])
        compiler, result = self.compile(['colour_in', 'colour_out', 'glass'], 'probe')
        self.assertEqual(mod.decode_png(compiler.textures[result['polygons'][0]['texture']][0])[2], bytes((249, 255, 254, 255)))

    def test_layer_union_does_not_duplicate_groups(self):
        _, result = self.compile(['glass', 'cube'], 'probe', layers=['cutout', 'translucent'])
        self.assertEqual(len(result['polygons']), 2)
        _, result = self.compile(['glass', 'cube'], 'probe', layers=['cutout'])
        self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [1])
        _, result = self.compile(['glass', 'amethyst', 'paper', 'base'], 'chunk_loader', layers=['cutout'])
        self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [1])

    def test_floodlight_replaces_application_rotation(self):
        model = self.model(['base'], 'floodlight')
        compiler = self.compiler()
        result = compiler.compile_model({'model': model, 'x': 90, 'y': 270}, selector={'facing': 'down'})
        self.assertEqual(result['rotation'], (0, 0, 0))
        self.assertEqual([v[:3] for v in result['polygons'][0]['vertices']], [(8, 6, 8), (8, -10, 8), (8, 6, 24)])
        result = compiler.compile_model({'model': model}, selector={'facing': 'up'})
        self.assertEqual([v[:3] for v in result['polygons'][0]['vertices']], [(8, 10, 8), (8, 26, 8), (8, 10, 24)])

    def test_structural_arm_exact_default_surface_and_shrink_order(self):
        compiler, result = self.compile(['Slope'], 'structural_arm')
        faces = result['polygons']
        self.assertEqual(len(faces), 10)
        self.assertEqual(compiler.element_count, 10)
        self.assertEqual(faces[0]['vertices'], [(0, 16, 0, 0, 0), (0, 0, 16, 0, 16), (16, 0, 16, 16, 16), (16, 16, 0, 16, 0)])
        self.assertEqual([len(face['vertices']) for face in faces], [4, 4, 4, 4, 3, 3, 3, 3, 4, 4])
        self.assertAlmostEqual(faces[8]['vertices'][0][0], .008)
        self.assertAlmostEqual(faces[9]['vertices'][0][0], .015992)
        self.assertEqual(faces[4]['vertices'], [(0, 0, 16, 0, 0), (0, 16, 0, 16, 16), (0, 0, 0, 16, 0)])

    def test_obj_forward_references_preserve_line_relative_negative_indices(self):
        model = self.model(['base'], 'default')
        source = (b'mtllib test.mtl\nusemtl surface\no base\n'
                  b'f 1/1 2/2 3/3\n'
                  b'v 0 0 0\nv 0 1 0\nv 0 0 1\nvt 0 0\nvt 1 0\nvt 0 1\n'
                  b'f -3/-3 -2/-2 -1/-1\n'
                  b'v 8 8 8\nvt .5 .5\n')
        put(self.source, 'assets/fixture/models/test.obj.ie', source)
        compiler = self.compiler()
        for loader in ('immersiveengineering:ie_obj', 'neoforge:obj'):
            result = compiler.compile_model({'model': {**model, 'loader': loader, 'flip_v': True}})
            self.assertEqual(len(result['polygons']), 2)
            self.assertEqual(result['polygons'][0]['vertices'], result['polygons'][1]['vertices'])
            self.assertEqual(result['polygons'][0]['vertices'][0], (0, 0, 0, 0, 16))
        for bad in (source.replace(b'f 1/1', b'f 5/1'), source.replace(b'f 1/1', b'f -1/1')):
            put(self.source, 'assets/fixture/models/test.obj.ie', bad)
            compiler = self.compiler()
            with self.assertRaisesRegex(mod.ImportError, 'index out of range'):
                compiler.compile_model({'model': model})

    def test_empty_static_models_do_not_hide_block_entity_renderers(self):
        empty = 'immersiveengineering:block/ie_empty'
        put(self.source, 'assets/immersiveengineering/models/block/ie_empty.json', {'elements': []})
        for name in ('bucket_wheel', 'watermill', 'windmill', 'turret_chem_top', 'turret_gun_top'):
            put(self.source, 'assets/immersiveengineering/models/block/' + name + '.json', {'parent': empty})
        compiler = self.compiler()
        definition = {'variants': {'': {'model': empty}}}
        rows = compiler.block('immersiveengineering:fake_light', definition)
        self.assertEqual(rows, [('immersiveengineering:fake_light', [0, 0, 0], 0, [])])
        for name in ('bucket_wheel', 'watermill', 'windmill', 'shader_banner', 'shader_banner_wall'):
            model = empty if name.startswith('shader_banner') else 'immersiveengineering:block/' + name
            with self.subTest(block=name), self.assertRaisesRegex(mod.ImportError, 'block-entity renderer'):
                compiler.block('immersiveengineering:' + name, {'variants': {'': {'model': model}}})
        for kind in ('chem', 'gun'):
            name = 'immersiveengineering:turret_' + kind
            top = 'immersiveengineering:block/turret_' + kind + '_top'
            rows = compiler.block(name, {'variants': {'multiblockslave=true': {'model': top}}})
            self.assertEqual(rows[0][2:], (0, []))
            with self.assertRaisesRegex(mod.ImportError, 'block-entity renderer'):
                compiler.block(name, {'variants': {'multiblockslave=false': {'model': top}}})
        # An actual replacement mesh takes precedence over the original empty parent.
        override = {'parent': empty, 'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16],
                    'faces': {'north': {'texture': 'fixture:white'}}}]}
        rows = compiler.block('immersiveengineering:windmill', {'variants': {'': {'model': override}}})
        self.assertTrue(rows[0][3][0]['elements'])

    def test_omitted_layers_mean_solid_only(self):
        model = self.model(['base', 'glass', 'amethyst', 'paper'], 'chunk_loader')
        del model['layers']
        compiler = self.compiler()
        result = compiler.compile_model({'model': model})
        self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [0])
        model['callbacks'] = 'immersiveengineering:cloche'
        result = compiler.compile_model({'model': model})
        self.assertEqual([p['vertices'][0][0] for p in result['polygons']], [0, 2, 3])

    def test_collinear_quad_triangle_preserves_other_fixed_diagonal_triangle(self):
        model = self.model(['base'], 'default')
        # Source corners0,1,2 are collinear. UV1 is intentionally off the
        # surviving triangle's interpolation, so choosing another diagonal
        # would produce the wrong texture even though the outline is equal.
        source = (b'mtllib test.mtl\nusemtl surface\no base\n'
                  b'v 0 0 0\nv .5 0 0\nv 1 0 0\nv 0 1 0\n'
                  b'vt 0 0\nvt .5 1\nvt 1 0\nvt 0 1\n'
                  b'f 1/1 2/2 3/3 4/4\n'
                  b'f 1/1 4/4 3/3 2/2\n'
                  b'f 1/1 2/2 3/3 2/2\n')
        put(self.source, 'assets/fixture/models/test.obj.ie', source)
        compiler = self.compiler()
        for loader in ('immersiveengineering:ie_obj', 'neoforge:obj'):
            result = compiler.compile_model({'model': {**model, 'loader': loader, 'flip_v': True}})
            self.assertEqual(len(result['polygons']), 2)
            self.assertEqual(result['polygons'][0]['vertices'], [(0, 0, 0, 0, 16), (16, 0, 0, 16, 16), (0, 16, 0, 0, 0)])
            self.assertEqual(result['polygons'][1]['vertices'], [(0, 0, 0, 0, 16), (0, 16, 0, 0, 0), (16, 0, 0, 16, 16)])
        self.assertTrue(any('degenerate OBJ faces' in w for w in compiler.warnings))

    def test_malformed_callbacks_and_paths_fail(self):
        for fields in ({'callbacks': 'other:callback'}, {'dynamic': 1}, {'layers': ['unknown']},
                       {'visibility': {'base': False}}, {'transform': {'scale': 2}}, {'model': 'fixture:models/../outside.obj'}):
            model = self.model(['base'], 'chute')
            model.update(fields)
            compiler = self.compiler()
            with self.assertRaises(mod.ImportError):
                compiler.compile_model({'model': model})


if __name__ == '__main__':
    unittest.main()
