"""Canonical variant keys must reach every palette expansion before overlap checks."""
import unittest
from itertools import islice, permutations
from unittest.mock import Mock
from mod_assets import mod


class CanonicalSelectors(unittest.TestCase):
    def compiler(self, model=None):
        compiler = mod.Compiler(None)
        compiler.model = Mock(return_value=model or {})
        compiler.compile_model = Mock(return_value={'invisible': True})
        return compiler

    def test_equivalent_aliases_are_one_state_without_mutating_input(self):
        compiler = self.compiler()
        definition = {'variants': {'z=1,a=2': {'model': 'fixture:a', 'y': 0},
                                   'a=2,z=1': {'y': 0, 'model': 'fixture:a'}}}
        rows = compiler.block('fixture:block', definition)
        self.assertEqual([row[0] for row in rows], ['fixture:block[a=2,z=1]'])
        self.assertEqual(len(definition['variants']), 2)
        compiler.compile_model.assert_called_once()

    def test_conflicting_aliases_and_json_number_types_are_rejected(self):
        for left, right in ((0, 90), (False, 0), (0, 0.0)):
            with self.subTest(left=left, right=right):
                compiler = self.compiler()
                with self.assertRaisesRegex(mod.ImportError, 'conflicting canonical'):
                    compiler.block('fixture:block', {'variants': {
                        'z=1,a=2': {'model': 'fixture:a', 'y': left},
                        'a=2,z=1': {'model': 'fixture:a', 'y': right}}})
                compiler.compile_model.assert_not_called()

    def test_raw_count_and_existing_property_validation(self):
        compiler = self.compiler()
        aliases = {','.join(f'{key}=0' for key in keys): {}
                   for keys in islice(permutations('abcdefg'), mod.MAX_STATES+1)}
        with self.assertRaisesRegex(mod.ImportError, 'excessive block variants'):
            compiler.block('fixture:block', {'variants': aliases})
        for key in (1, 'a=1,a=2', 'a', ','.join(f'p{i}=0' for i in range(65))):
            with self.subTest(key=key), self.assertRaises(mod.ImportError):
                compiler.block('fixture:block', {'variants': {key: {}}})

    def test_unsorted_chest_and_dynamic_flags_have_no_duplicate_state(self):
        cases = [({'loader': mod._chest_module.LOADER}, 20),
                 ({'loader': mod._storage_module.BARREL_LOADER}, 3)]
        for model, expected in cases:
            with self.subTest(model=model):
                rows = self.compiler(model).block('fixture:block', {
                    'variants': {'z=1,a=2': {'model': 'fixture:model'}}})
                self.assertEqual(len(rows), expected)
                self.assertEqual(len({row[0] for row in rows}), expected)

    def test_twilight_partial_selector_cannot_hide_explicit_overlap(self):
        compiler = self.compiler({'_builtin_entity': True})
        block = 'twilightforest:canopy_hanging_sign'
        with self.assertRaisesRegex(mod.ImportError, 'overlapping Twilight sign'):
            compiler.block(block, {'variants': {
                'waterlogged=false': {'model': 'fixture:first'},
                'waterlogged=false,rotation=7': {'model': 'fixture:second'}}})
        compiler.compile_model.assert_not_called()
        rows = compiler.block(block, {'variants': {
            'waterlogged=false,attached=true': {'model': 'fixture:first'}}})
        self.assertEqual(len(rows), 17)
        self.assertEqual(len({row[0] for row in rows}), 17)


if __name__ == '__main__':
    unittest.main()
