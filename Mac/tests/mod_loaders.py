"""Asset-backed loader regressions with original geometry and pixels only."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put, simple


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mineways-loaders-')
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        simple(self.source)

    def tearDown(self):
        self.temporary.cleanup()

    def model(self, name, data):
        namespace, path = name.split(':')
        put(self.source, f'assets/{namespace}/models/{path}.json', data)

    def compiler(self):
        assets = mod.Assets([self.source])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def box(self, width=16, texture='sample:block/stone'):
        return {'elements': [{'from': [0, 0, 0], 'to': [width, 16, 16],
                             'faces': {'north': {'texture': texture, 'uv': [0, 0, 16, 16]}}}]}

    def test_table_uses_palette_type_and_retains_outer_rotation(self):
        kinds = ('none', 'one', 'straight', 'curve', 'three', 'all')
        self.model('sample:table', {'loader': 'bibliocraft:table',
                                  **{kind: self.box(i + 1) for i, kind in enumerate(kinds)}})
        compiler = self.compiler()
        for i, kind in enumerate(kinds):
            compiled = compiler.compile_model({'model': 'sample:table', 'x': 90, 'y': 270, 'uvlock': True}, selector={'type': kind})
            self.assertEqual(compiled['rotation'], (90, 270, 1))
            self.assertEqual(compiled['elements'][0]['coordinates'][:6], (0, 0, 0, i + 1, 16, 16))
        with self.assertRaisesRegex(mod.ImportError, 'table type'):
            compiler.compile_model({'model': 'sample:table'}, selector={'type': 'unsupported'})
        self.assertFalse(compiler.warnings)

    def test_bookcase_inherits_base_textures_without_inventing_books(self):
        self.model('sample:bookcase_template', {'loader': 'bibliocraft:bookcase',
                                              **self.box(9, '#wood'), 'book_0': {'parent': 'absent:inventory'}})
        self.model('sample:bookcase', {'parent': 'sample:bookcase_template',
                                     'textures': {'wood': 'sample:block/stone'}})
        compiler = self.compiler()
        result = compiler.compile_model({'model': 'sample:bookcase'})
        self.assertEqual(len(result['elements']), 1)
        self.assertEqual(result['elements'][0]['coordinates'][3], 9)
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'sample:block/stone')
        self.assertTrue(any('inventory books' in warning for warning in compiler.warnings))

    def test_cable_core_survives_composite_device_and_reports_connections(self):
        self.model('refinedstorage:block/cable/core/blue', self.box(4))
        self.model('sample:device', {'loader': 'neoforge:composite', 'children': {
            'cable': {'loader': 'refinedstorage:cable', 'color': 'blue'}, 'device': self.box(12)}})
        compiler = self.compiler()
        result = compiler.compile_model({'model': 'sample:device', 'y': 180})
        self.assertEqual(result['rotation'], (0, 180, 0))
        self.assertEqual([element['coordinates'][3] for element in result['elements']], [4, 12])
        self.assertTrue(any('neighbor connections' in warning for warning in compiler.warnings))
        with self.assertRaisesRegex(mod.ImportError, 'invalid Refined Storage cable color'):
            compiler.compile_model({'model': {'loader': 'refinedstorage:cable', 'color': '../blue'}})
        with self.assertRaisesRegex(mod.ImportError, 'missing model'):
            compiler.compile_model({'model': {'loader': 'refinedstorage:cable', 'color': 'red'}})

    def test_xycraft_single_texture_layers_face_mask_and_pixels(self):
        for name, color in [('base', (0, 0, 255, 255)), ('top', (255, 0, 0, 128))]:
            put(self.source, f'assets/sample/textures/block/{name}.png', mod.encode_png(2, 2, bytes(color) * 4))
        self.model('sample:connected', {'loader': 'xycraft_core:connected_textures',
                                       'faces': ['north', 'up'], 'base_tint_index': 0,
                                       'textures': {'base': 'sample:block/base', 'texture_single': '#top',
                                                    'top': 'sample:block/top'}})
        compiler = self.compiler()
        result = compiler.compile_model({'model': 'sample:connected', 'y': 90})
        self.assertEqual(result['rotation'], (0, 90, 0))
        faces = result['elements'][0]['faces']
        self.assertEqual([face[0] for face in faces], ['north', 'up'])
        self.assertTrue(all(face[1:5] == (0, 0, 16, 16) and face[-1] == face[0] for face in faces))
        texture = compiler.textures[faces[0][-2]][0]
        width, height, pixels = mod.decode_png(texture)
        self.assertEqual((width, height, pixels), (2, 2, bytes((128, 0, 127, 255)) * 4))
        self.assertTrue(any('dynamic tint' in warning for warning in compiler.warnings))

    def test_loader_cycles_transforms_missing_faces_and_aliases_fail(self):
        self.model('sample:cycle', {'loader': 'bibliocraft:table', 'none': {'parent': 'sample:cycle'}})
        compiler = self.compiler()
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            compiler.compile_model({'model': 'sample:cycle'})
        for model, reason in [
            ({'loader': 'bibliocraft:table', 'none': self.box(), 'transform': {}}, 'root transforms'),
            ({'loader': 'bibliocraft:table'}, 'missing Bibliocraft table child'),
            ({'loader': 'xycraft_core:connected_textures', 'faces': ['diagonal']}, 'face mask'),
            ({'loader': 'xycraft_core:connected_textures', 'textures': {}}, 'missing custom-loader texture'),
            ({'loader': 'xycraft_core:connected_textures', 'textures': {'texture_single': '#texture_single'}}, 'cyclic'),
        ]:
            with self.subTest(reason=reason), self.assertRaisesRegex(mod.ImportError, reason):
                compiler.compile_model({'model': model})

    def test_cached_fusion_parent_keeps_warning_after_failed_child(self):
        self.model('sample:block/connected_template', {'loader': 'fusion:model', 'type': 'connecting',
                                                      'parent': 'minecraft:block/cube_all'})
        for block, texture in [('a_bad', 'sample:block/missing'), ('z_good', 'sample:block/stone')]:
            self.model('sample:block/' + block, {'parent': 'sample:block/connected_template',
                                                'textures': {'all': texture}})
            put(self.source, f'assets/sample/blockstates/{block}.json',
                {'variants': {'': {'model': 'sample:block/' + block}}})
        report = mod.compile_pack([self.source], self.root / 'output')
        self.assertEqual(report['blocks'], 2)
        self.assertEqual(report['unsupported'][0]['block'], 'sample:a_bad')
        self.assertEqual(report['warnings'],
                         ['sample:block/z_good: connected-texture neighbors are not evaluated'])

    def test_loader_pack_determinism_and_texture_override(self):
        self.model('sample:table', {'loader': 'bibliocraft:table', 'none': self.box(7)})
        put(self.source, 'assets/sample/blockstates/table.json', {'variants': {'type=none': {'model': 'sample:table'}}})
        override = self.root / 'override'
        put(override, 'assets/sample/textures/block/stone.png', mod.encode_png(2, 2, bytes((1, 2, 3, 255)) * 4))
        first, second = self.root / 'first', self.root / 'second'
        a = mod.compile_pack([self.source, override], first)
        b = mod.compile_pack([self.source, override], second)
        self.assertEqual(a, b)
        self.assertEqual(a['blocks'], 2)
        manifest = (first / 'manifest.mwpack').read_text()
        self.assertIn('state\tsample:table[type=none]\t010203\t255', manifest)
        for file in first.rglob('*'):
            if file.is_file():
                self.assertEqual(file.read_bytes(), (second / file.relative_to(first)).read_bytes())


if __name__ == '__main__':
    unittest.main()
