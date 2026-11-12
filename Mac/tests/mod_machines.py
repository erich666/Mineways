"""MI machine adapter regressions with original synthetic assets only."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put, simple


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

    def test_casing_and_north_fallback_preserve_application_rotation(self):
        compiler = self.compiler()
        model = self.machine(default_overlays={'front': 'sample:block/stone',
                                               'front_active': 'missing:active',
                                               'output': 'missing:output'})
        compiled = compiler.compile_model({'model': model, 'y': 90, 'uvlock': True},
                                          selector={'facing': 'east', 'active': 'true'})
        self.assertEqual(compiled['rotation'], (0, 90, 1))
        self.assertEqual(len(compiled['elements']), 2)
        overlay = compiled['elements'][1]
        self.assertEqual(overlay['coordinates'][:6], (0, 0, -0.008, 16, 16, -0.008))
        self.assertEqual(overlay['faces'][0], ('north', 0, 0, 16, 16, 0, 'sample:block/stone', 'north'))
        self.assertTrue(any('north-facing inactive' in text and 'block entity orientation' in text
                            for text in compiler.warnings))

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
