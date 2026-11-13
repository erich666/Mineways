"""Verified default barrel parts retain acacia overrides and tier inheritance."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_storage import storage
from mod_storage_fixtures import limited_barrel_assets


class LimitedBarrelModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent, self.tier, self.flat, self.definition = limited_barrel_assets(self.root, mod.encode_png)

    def compile(self, reference='fixture:limited_barrel', rotation=(0, 0, 0), paths=None):
        compiler = mod.Compiler(mod.Assets(paths or [self.root]))
        result = storage.compile_limited_barrel(compiler, reference, {}, rotation, {}, 0,
                                               mod.ImportError, 'fixture:limited_barrel')
        return compiler, result

    def test_block_dispatch_keeps_all_three_supplied_variants(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        rows = compiler.block('fixture:limited_barrel', self.definition)
        self.assertEqual(len(rows), 3)
        self.assertFalse(compiler.unsupported_states)
        for selector, _, _, models in rows:
            self.assertEqual(len(models[0]['elements']), 2)
            flat = 'flat_top=true' in selector
            self.assertEqual(models[0]['elements'][0]['coordinates'][4], 16 if flat else 14)
            expected = (180, 270, 0) if flat else ((90, 90, 0) if 'facing=east' in selector else (0, 0, 0))
            self.assertEqual(models[0]['rotation'], expected)

    def test_nested_simple_composite_flattens_with_one_part_texture_context(self):
        # CompositeElementsModel uses getElements(), not each child's bake().
        # Visibility and root transforms on these children do not affect extraction.
        composite = {'loader': 'sophisticatedstorage:simple_composite', 'parts': {
            'first': {'parent': 'fixture:barrel_recessed', 'textures': {'surface': 'fixture:barrel_unused'}},
            'nested': {'loader': 'sophisticatedstorage:simple_composite', 'parts': {
                'trim': {'parent': 'fixture:barrel_trim', 'transform': {'translation': [5, 0, 0]}}},
                'visibility': {'trim': False}}}}
        put(self.root, 'assets/fixture/models/compound.json', composite)
        source = copy.deepcopy(self.parent)
        source['model_parts']['base']['model'] = 'fixture:compound'
        _, result = self.compile(source)
        self.assertEqual(len(result['elements']), 3)
        self.assertEqual(result['elements'][1]['coordinates'][:6], (2, 14, 3, 14, 16, 15))
        self.assertTrue(all(face[-2] == 'fixture:barrel_wood' for element in result['elements'][:2] for face in element['faces']))
        composite['parts']['nested'] = {'parent': 'fixture:compound'}
        put(self.root, 'assets/fixture/models/compound.json', composite)
        with self.assertRaisesRegex(mod.ImportError, 'cyclic'):
            self.compile(source)
        for parts in ({}, [], {'bad': 'fixture:barrel_trim'}):
            put(self.root, 'assets/fixture/models/compound.json', {**composite, 'parts': parts})
            with self.subTest(parts=parts), self.assertRaises(mod.ImportError):
                self.compile(source)

    def test_new_composite_parts_replace_inherited_custom_parts_only(self):
        loader = 'sophisticatedstorage:simple_composite'
        standard = {'from': [0, 1, 2], 'to': [3, 4, 5], 'faces': {'up': {'texture': '#surface'}}}
        put(self.root, 'assets/fixture/models/parent_compound.json', {
            'loader': loader, 'elements': [standard],
            'parts': {'old': {'parent': 'fixture:barrel_recessed'}}})
        put(self.root, 'assets/fixture/models/child_compound.json', {
            'loader': loader, 'parent': 'fixture:parent_compound',
            'parts': {'new': {'parent': 'fixture:barrel_trim'}}})
        compiler = mod.Compiler(mod.Assets([self.root]))
        parsed = storage._barrel_geometry(compiler, 'fixture:child_compound', mod.ImportError)
        self.assertEqual([element['from'] for element in parsed['elements']], [[0, 1, 2], [2, 14, 3]])
        # A plain child still inherits the parent's custom children.
        parsed = storage._barrel_geometry(compiler, {'parent': 'fixture:parent_compound'}, mod.ImportError)
        self.assertEqual([element['from'] for element in parsed['elements']], [[0, 1, 2], [1, 0, 2]])

    def test_inherited_model_and_texture_only_tier_override(self):
        compiler, result = self.compile()
        self.assertEqual(len(result['elements']), 2)
        base, tier = result['elements']
        self.assertEqual(base['coordinates'][:6], (1, 0, 2, 15, 14, 16))
        self.assertEqual(tier['coordinates'][:6], (2, 14, 3, 14, 16, 15))
        self.assertEqual(base['faces'][0][:6], ('north', 1, 2, 13, 14, 0))
        self.assertEqual(tier['faces'][1][:6], ('up', 2, 3, 14, 15, 90))
        self.assertTrue(all(f[-2] == 'fixture:barrel_wood' for f in base['faces']))
        self.assertTrue(all(f[-2] == 'fixture:barrel_tier' for f in tier['faces']))
        self.assertEqual(set(compiler.textures), {'fixture:barrel_wood', 'fixture:barrel_tier'})
        self.assertTrue(any('saved wood, dyes, custom materials' in w for w in compiler.warnings))

    def test_flat_geometry_and_variant_rotations(self):
        _, flat = self.compile('fixture:limited_barrel_flat', (180, 270, 1))
        self.assertEqual(flat['elements'][0]['coordinates'][:6], (0, 0, 0, 16, 16, 16))
        self.assertEqual(flat['rotation'], (180, 270, 1))
        _, normal = self.compile(rotation=(90, 90, 0))
        self.assertEqual(normal['rotation'], (90, 90, 0))
        self.assertEqual(normal['elements'][0]['coordinates'][4], 14)

    def test_child_defaults_override_parent_wood_then_child_wood_overrides_both(self):
        source = copy.deepcopy(self.tier)
        source['model_parts']['base'] = {'textures': {'surface': 'fixture:barrel_tier'}}
        _, result = self.compile(source)
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixture:barrel_tier')
        source['wood_overrides'] = {'acacia': {'base': {'textures': {'surface': 'fixture:barrel_wood'}}}}
        _, result = self.compile(source)
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixture:barrel_wood')

    def test_resource_override_controls_referenced_part_geometry(self):
        override = self.root / 'overrides'
        put(override, 'assets/fixture/models/barrel_recessed.json', {'elements': [
            {'from': [3, 4, 5], 'to': [8, 9, 10], 'faces': {'west': {'texture': '#surface'}}}]})
        _, result = self.compile(paths=[self.root, override])
        self.assertEqual(result['elements'][0]['coordinates'][:6], (3, 4, 5, 8, 9, 10))
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixture:barrel_wood')

    def test_invalid_overrides_do_not_get_silently_ignored(self):
        cases = [{'wood_overrides': []}, {'wood_overrides': {'acacia': []}},
                 {'wood_overrides': {'oak': {'base': []}}},
                 {'wood_overrides': {'acacia': {'base': {'textures': []}}}},
                 {'model_parts': {'base': {'model': '../outside'}}},
                 {'model_parts': {'tier': {'textures': {'surface': None}}}},
                 {'model_parts': {'tier': {'textures': {str(i): 'fixture:barrel_tier' for i in range(257)}}}},
                 {'transform': {'translation': [1, 0, 0]}}]
        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(mod.ImportError):
                self.compile({**self.tier, **changes})

    def test_missing_models_cycles_and_parent_depth_are_bounded(self):
        with self.assertRaises(mod.ImportError):
            self.compile({'loader': storage.LIMITED_BARREL_LOADER, 'model_parts': {'base': {'textures': {}}}})
        put(self.root, 'assets/fixture/models/cycle.json', {'loader': storage.LIMITED_BARREL_LOADER, 'parent': 'fixture:cycle'})
        with self.assertRaisesRegex(mod.ImportError, 'cyclic'):
            self.compile('fixture:cycle')
        for i in range(65):
            put(self.root, f'assets/fixture/models/depth{i}.json', {'loader': storage.LIMITED_BARREL_LOADER,
                'parent': f'fixture:depth{i+1}' if i < 64 else 'fixture:limited_barrel'})
        with self.assertRaisesRegex(mod.ImportError, 'excessive'):
            self.compile('fixture:depth0')

    def test_combined_geometry_limit_applies_to_both_parts(self):
        element = {'from': [0, 0, 0], 'to': [1, 1, 1], 'faces': {'up': {'texture': '#surface'}}}
        put(self.root, 'assets/fixture/models/large.json', {'elements': [element] * 1025})
        source = copy.deepcopy(self.parent)
        for part in ('base', 'tier'):
            source['model_parts'][part]['model'] = 'fixture:large'
        with self.assertRaisesRegex(mod.ImportError, 'composite element count'):
            self.compile(source)


if __name__ == '__main__':
    unittest.main()
