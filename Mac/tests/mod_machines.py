"""MI machine adapter regressions with original synthetic assets only."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put, simple
from mod_machine_fixtures import machine_assets


class MachineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mineways-machines-')
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        simple(self.source)
        self.model('modern_industrialization:machine_casing/lv', {
            'loader': 'modern_industrialization:use_block_model', 'block': 'sample:stone'})

    def tearDown(self):
        self.temporary.cleanup()

    def model(self, name, data):
        namespace, path = name.split(':')
        put(self.source, f'assets/{namespace}/models/{path}.json', data)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.source])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def machine(self, **extra):
        return {'loader': 'modern_industrialization:machine', 'casing': 'lv',
                'default_overlays': {'front': 'sample:block/stone'}, **extra}

    def test_verified_empty_fluid_identities_and_narrow_air_scope(self):
        self.model('minecraft:block/air', {})
        compiler = self.compiler()
        names = mod._machine_module.EMPTY_FLUID_BLOCKS
        self.assertEqual(len(names), 81)
        definition = {'variants': {'custom=kept': {'model': 'minecraft:block/air'}}}
        for name in names:
            self.assertEqual(compiler.block(name, definition), [(name + '[custom=kept]', [0, 0, 0], 0, [])])
        for name in ('create:copycat_panel', 'copycats:copycat_slab', 'modern_industrialization:unknown_fluid'):
            with self.subTest(name=name), self.assertRaises(mod.ImportError):
                compiler.block(name, definition)
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model': {}}, block_name='modern_industrialization:hydrogen')
        self.assertEqual(compiler.textures, {})

    def test_empty_fluid_explicit_visible_overrides_remain_visible(self):
        self.model('minecraft:block/air', {'parent': 'minecraft:block/cube_all',
                                         'textures': {'all': 'sample:block/stone'}})
        compiler = self.compiler()
        for name in ('modern_industrialization:hydrogen', 'extended_industrialization:honey'):
            model = compiler.compile_model({'model': 'minecraft:block/air'}, block_name=name)
            self.assertFalse(model.get('invisible'))
            self.assertEqual(len(model['elements'][0]['faces']), 6)

    def test_casing_and_north_fallback_ignore_application_rotation(self):
        compiler = self.compiler()
        model = self.machine(default_overlays={'front': 'sample:block/stone',
                                               'front_active': 'missing:active',
                                               'output': 'missing:output'})
        compiled = compiler.compile_model({'model': model, 'y': 90, 'uvlock': True},
                                          selector={'facing': 'east', 'active': 'true'})
        self.assertEqual(compiled['rotation'], (0, 0, 0))
        self.assertTrue(compiled['fixed_rotation'])
        self.assertEqual(len(compiled['elements']), 2)
        overlay = compiled['elements'][1]
        self.assertEqual(overlay['coordinates'][:6], (0, 0, -0.008, 16, 16, -0.008))
        self.assertEqual(overlay['faces'][0], ('north', 0, 0, 16, 16, 0, 'sample:block/stone', 'north'))
        self.assertTrue(any('north-facing inactive' in text and 'block entity orientation' in text
                            for text in compiler.warnings))

    def test_both_machine_loaders_ignore_xyz_and_uvlock_and_reject_rotated_composites(self):
        compiler = self.compiler()
        for model in (self.machine(), {'loader': 'modern_industrialization:use_block_model', 'block': 'sample:stone'}):
            expected = compiler.compile_model({'model': model})
            for turns in ({'x': 90, 'y': 270, 'uvlock': True}, {'ae2:z': 90}):
                result = compiler.compile_model({'model': model, **turns})
                self.assertEqual(result, expected)
                self.assertEqual(result['rotation'], (0, 0, 0))
                self.assertTrue(result['fixed_rotation'])
            composite = {'loader': 'neoforge:composite', 'children': {'machine': model}}
            nested = {'loader': 'neoforge:composite', 'children': {'inner': composite}}
            self.assertTrue(compiler.compile_model({'model': nested})['fixed_rotation'])
            with self.assertRaisesRegex(mod.ImportError, 'fixed-rotation'):
                compiler.compile_model({'model': nested, 'y': 90})

    def test_shared_machine_fixture_preserves_identity_for_both_loaders(self):
        machine_assets(self.source, mod.encode_png)
        compiler = self.compiler()
        for name, face_count in (('machine_default', 3), ('machine_delegate', 2)):
            rows = compiler.block('fixture:' + name, compiler.assets.json(f'assets/fixture/blockstates/{name}.json'))
            self.assertEqual(len(rows), 2)
            self.assertFalse(compiler.unsupported_states)
            self.assertEqual(rows[0][3], rows[1][3])
            model = rows[0][3][0]
            self.assertEqual(model['rotation'], (0, 0, 0))
            self.assertEqual(sum(len(element['faces']) for element in model['elements']), face_count)
            self.assertEqual(model['elements'][0]['coordinates'][:6], (1, 2, 3, 14, 15, 16))

    def test_all_overlay_directions_and_specific_field_precedence(self):
        overlays = {'front': 'sample:block/stone', 'side': 'sample:block/stone',
                    'top_n': 'sample:block/stone', 'top': 'missing:unused',
                    'bottom_n': 'sample:block/stone', 'bottom': 'missing:unused'}
        compiled = self.compiler().compile_model({'model': self.machine(default_overlays=overlays)})
        by_face = {element['faces'][0][0]: element for element in compiled['elements'][1:]}
        self.assertEqual(set(by_face), {'north', 'south', 'west', 'east', 'up', 'down'})
        for face, axis, position in [('north', 2, -0.008), ('south', 2, 16.008),
                                     ('west', 0, -0.008), ('east', 0, 16.008),
                                     ('down', 1, -0.008), ('up', 1, 16.008)]:
            element = by_face[face]
            self.assertEqual(element['coordinates'][axis], position)
            self.assertEqual(element['coordinates'][axis + 3], position)
            self.assertEqual(element['faces'][0][1:6], (0, 0, 16, 16, 0))

    def test_tier_merge_null_inheritance_and_empty_overlays(self):
        model = self.machine(default_overlays={'front': 'missing:default', 'top': 'sample:block/stone'},
                             tiered_overlays={'modern_industrialization:lv': {
                                 'front': 'sample:block/stone', 'top': None},
                                 'hv': {'front': 'missing:unselected'}})
        result = self.compiler().compile_model({'model': model})
        self.assertEqual(len(result['elements']), 3)
        empty = self.compiler().compile_model({'model': self.machine(default_overlays={})})
        self.assertEqual(len(empty['elements']), 1)

    def test_namespaced_casing_and_resource_override(self):
        self.model('extended:machine_casing/steel', {'parent': 'sample:block/stone'})
        override = self.root / 'override'
        put(override, 'assets/sample/models/block/stone.json', {
            'elements': [{'from': [0, 0, 0], 'to': [4, 16, 16],
                          'faces': {'north': {'texture': 'sample:block/stone'}}}]})
        result = self.compiler([self.source, override]).compile_model({
            'model': self.machine(casing='extended:steel', default_overlays={})})
        self.assertEqual(result['elements'][0]['coordinates'][3], 4)

    def test_missing_ambiguous_rotated_or_recursive_casing_is_rejected(self):
        for definition in [
            {'variants': {'facing=north': {'model': 'sample:block/stone'}}},
            {'variants': {'': {'model': 'sample:block/stone', 'y': 90}}},
            {'variants': {'': [{'model': 'sample:block/stone'}]}},
        ]:
            put(self.source, 'assets/sample/blockstates/stone.json', definition)
            with self.assertRaisesRegex(mod.ImportError, 'machine casing'):
                self.compiler().compile_model({'model': self.machine()})
        put(self.source, 'assets/sample/blockstates/stone.json', {'variants': {'': {'model': 'sample:cycle'}}})
        self.model('sample:cycle', self.machine())
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compiler().compile_model({'model': self.machine()})
        with self.assertRaisesRegex(mod.ImportError, 'missing model'):
            self.compiler().compile_model({'model': self.machine(casing='missing:casing')})

    def test_malformed_inputs_fail_without_arbitrary_fallback(self):
        for model, reason in [
            (self.machine(casing='../outside'), 'invalid machine resource'),
            (self.machine(default_overlays=[]), 'overlays must be objects'),
            (self.machine(default_overlays={'front': '../outside'}), 'invalid machine resource'),
            (self.machine(default_overlays={'front': 'missing:front'}), 'missing texture'),
            (self.machine(no_overlay_on_output_side='true'), 'must be boolean'),
            (self.machine(transform={}), 'root transforms'),
        ]:
            with self.subTest(reason=reason), self.assertRaisesRegex(mod.ImportError, reason):
                self.compiler().compile_model({'model': model})


if __name__ == '__main__':
    unittest.main()
