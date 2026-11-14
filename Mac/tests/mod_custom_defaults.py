"""Verified framed child geometry and identity-oriented microchip defaults."""
import copy
import tempfile
import unittest
from pathlib import Path

from mod_assets import mod, put
from mod_custom_default_fixtures import custom_default_assets


class CustomDefaults(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.front, self.side = custom_default_assets(self.root, mod.encode_png)

    def compiler(self, paths=None):
        return mod.Compiler(mod.Assets(paths or [self.root]))

    def test_framed_child_scopes_world_children_and_variant_turns(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:framed_default', compiler.assets.json('assets/fixture/blockstates/framed_default.json'))
        self.assertEqual(len(rows), 2)
        self.assertFalse(compiler.unsupported_states)
        expected = compiler.compile_model({'model': 'fixture:framed_front'})['elements'] + compiler.compile_model({'model': 'fixture:framed_side'})['elements']
        for name, _, _, models in rows:
            self.assertEqual(models[0]['elements'], expected)
            self.assertEqual(models[0]['rotation'], (0, 90, 1) if 'east' in name else (0, 0, 0))
        self.assertEqual(set(compiler.textures), {'fixture:framed_front', 'fixture:framed_side'})
        self.assertTrue(any('saved block-entity design materials' in w for w in compiler.warnings))

    def test_legacy_parts_visibility_and_hidden_invalid_child(self):
        compiler = self.compiler()
        model = {'loader': 'functionalstorage:framedblock', 'parts': {
            'front': {'parent': 'fixture:framed_front'}, 'hidden': {'parent': 'fixture:absent'}},
            'visibility': {'hidden': False}, 'item_render_order': ['front']}
        result = compiler.compile_model({'model': model})
        self.assertEqual(len(result['elements']), 1)
        self.assertEqual(result['elements'][0]['faces'][0][6], 'fixture:framed_front')

    def test_framed_malformed_or_excessive_children_fail(self):
        child = {'parent': 'fixture:framed_front'}
        examples = [({}, '1..256'), ({'children': []}, 'objects'),
            ({'children': {'x': child}, 'parts': {'x': child}}, 'duplicate'),
            ({'children': {'x': 'fixture:framed_front'}}, 'inline'),
            ({'children': {str(i): child for i in range(257)}}, '1..256'),
            ({'children': {'x': child}, 'item_render_order': ['absent']}, 'item_render_order'),
            ({'children': {'x': child}, 'visibility': {'x': 1}}, 'visibility'),
            ({'children': {'x': child}, 'visibility': {'x': False}}, 'hidden child'),
            ({'children': {'x': child}, 'item_render_order': None}, 'item_render_order')]
        for index, (fields, error) in enumerate(examples):
            with self.subTest(case=index), self.assertRaisesRegex(mod.ImportError, error):
                self.compiler().compile_model({'model': {'loader': 'functionalstorage:framedblock', **fields}})

    def test_framed_loader_redefinition_does_not_inherit_children_or_item_order(self):
        compiler = self.compiler()
        inherited = compiler.compile_model({'model': {'parent': 'fixture:framed_default'}})
        self.assertEqual(len(inherited['elements']), 2)
        ignored = compiler.compile_model({'model': {'parent': 'fixture:framed_default',
            'children': {'ignored': {'parent': 'fixture:absent'}}, 'parts': {'also_ignored': {}},
            'item_render_order': ['absent']}})
        self.assertEqual(ignored, inherited)
        with self.assertRaisesRegex(mod.ImportError, '1..256'):
            compiler.compile_model({'model': {'parent': 'fixture:framed_default', 'loader': 'functionalstorage:framedblock'}})
        replacement = compiler.compile_model({'model': {'parent': 'fixture:framed_default',
            'loader': 'functionalstorage:framedblock', 'parts': {'new': {'parent': 'fixture:framed_side'}}}})
        self.assertEqual(len(replacement['elements']), 1)
        self.assertEqual(len(replacement['elements'][0]['faces']), 2)

    def test_framed_transform_rejected_instead_of_dropped_and_cache_unchanged(self):
        compiler = self.compiler()
        original = copy.deepcopy(compiler.model('fixture:framed_default'))
        with self.assertRaisesRegex(mod.ImportError, 'transformed composites'):
            compiler.compile_model({'model': {'parent': 'fixture:framed_default', 'transform': {'translation': [1, 0, 0]}}})
        compiler.compile_model({'model': 'fixture:framed_default'})
        self.assertEqual(compiler.model('fixture:framed_default'), original)

    def test_microchip_exact_cube_identity_uvs_and_uncullable_faces(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:microchip_default', compiler.assets.json('assets/fixture/blockstates/microchip_default.json'))
        self.assertEqual(len(rows), 2)
        self.assertFalse(compiler.unsupported_states)
        self.assertEqual(rows[0][3], rows[1][3])
        for _, _, _, models in rows:
            self.assertEqual(models[0]['rotation'], (0, 0, 0))
            element = models[0]['elements'][0]
            self.assertEqual(element['coordinates'][:6], (0, 0, 0, 16, 16, 16))
            self.assertEqual(len(element['faces']), 6)
            for face in element['faces']:
                self.assertEqual(face[1:6], (0, 0, 16, 16, 0))
                self.assertEqual(face[6], 'fixture:microchip_top' if face[0] == 'up' else 'fixture:microchip_base')
                self.assertEqual(face[7], '-')
        self.assertEqual(set(compiler.textures), {'fixture:microchip_base', 'fixture:microchip_top'})
        self.assertTrue(any('saved signal-side configuration' in w for w in compiler.warnings))

    def test_microchip_directional_unresolved_alias_and_missingno_use_base(self):
        compiler = self.compiler()
        result = compiler.compile_model({'model': {'loader': 'little_big_redstone:microchip', 'textures': {
            'base': 'fixture:microchip_base', 'base_up': '#absent', 'base_down': 'minecraft:missingno'}}})
        self.assertTrue(all(face[6] == 'fixture:microchip_base' for face in result['elements'][0]['faces']))
        for value in ('#base_up', 12):
            with self.subTest(value=value), self.assertRaises(mod.ImportError):
                compiler.compile_model({'model': {'loader': 'little_big_redstone:microchip',
                    'textures': {'base': 'fixture:microchip_base', 'base_up': value}}})

    def test_microchip_ignores_unused_geometry_transform_and_visibility(self):
        compiler = self.compiler()
        result = compiler.compile_model({'model': {'parent': 'fixture:microchip_default',
            'elements': [{'invalid': True}], 'transform': {'translation': [10, 0, 0]}, 'visibility': {'all': False}},
            'x': 90, 'y': 180, 'ae2:z': 90, 'uvlock': True})
        self.assertEqual(result['rotation'], (0, 0, 0))
        self.assertEqual(len(result['elements'][0]['faces']), 6)

    def test_supplied_texture_and_model_overrides_are_respected(self):
        override = self.root / 'override'
        put(override, 'assets/fixture/models/framed_side.json', self.front)
        put(override, 'assets/fixture/models/microchip_default.json', self.side)
        compiler = self.compiler([self.root, override])
        framed = compiler.compile_model({'model': 'fixture:framed_default'})
        self.assertTrue(all(f[6] == 'fixture:framed_front' for e in framed['elements'] for f in e['faces']))
        chip = compiler.compile_model({'model': 'fixture:microchip_default', 'y': 90})
        self.assertEqual(chip['rotation'], (0, 90, 0))
        self.assertEqual(len(chip['elements'][0]['faces']), 2)
        self.assertFalse(any('microchip default' in w for w in compiler.warnings))

    def test_fixed_microchip_rotation_survives_nested_composites(self):
        compiler = self.compiler()
        inner = {'loader': 'neoforge:composite', 'children': {'chip': {'parent': 'fixture:microchip_default'}}}
        outer = {'loader': 'neoforge:composite', 'children': {'inner': inner}}
        for model in (inner, outer):
            result = compiler.compile_model({'model': model})
            self.assertTrue(result['fixed_rotation'])
            self.assertEqual(len(result['elements'][0]['faces']), 6)
            for turns in ({'y': 90}, {'x': 180}, {'ae2:z': 90}):
                with self.subTest(turns=turns), self.assertRaisesRegex(mod.ImportError, 'rotation'):
                    compiler.compile_model({'model': model, **turns})

    def test_shared_fixture_serializes_four_states(self):
        output = self.root / 'compiled'
        report = mod.compile_pack([self.root], output)
        self.assertFalse(report['unsupported'])
        self.assertFalse(report['unsupported_states'])
        manifest = (output / 'manifest.mwpack').read_text()
        self.assertEqual(manifest.count('\nstate\t'), 4)
        self.assertNotIn('\ninvisible\n', manifest)


if __name__ == '__main__':
    unittest.main()
