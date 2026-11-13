"""Data-model fallbacks preserve palette flags, source geometry, and texture scope."""
import json
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod
from mod_data_storage_fixtures import data_storage_assets


class DataStorage(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.wrapper = data_storage_assets(self.root, mod.encode_png)

    def compiler(self):
        return mod.Compiler(mod.Assets([self.root]))

    def put(self, reference, value):
        path = self.root / mod.asset_path(reference, 'models')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def test_qio_all_orientations_and_active_distinction(self):
        compiler = self.compiler()
        rows = compiler.block('fixturedata:qio', compiler.assets.json('assets/fixturedata/blockstates/qio.json'))
        self.assertEqual(len(rows), 12)
        self.assertFalse(compiler.unsupported_states)
        for state, _, _, models in rows:
            expected = 'fixturedata:online' if 'active=true' in state else 'fixturedata:offline'
            self.assertEqual(models[0]['elements'][0]['faces'][0][-2], expected)
            self.assertEqual(models[0]['elements'][0]['coordinates'][:6], (2, 3, 4, 11, 13, 15))
        self.assertNotIn('fixturedata:emitting', compiler.textures)
        self.assertTrue(any('emitting-redstone' in warning for warning in compiler.warnings))
        model = compiler.compile_model({'model': 'fixturedata:qio', 'x': 90, 'y': 270, 'uvlock': True})
        self.assertEqual(model['rotation'], (90, 270, 1))

    def test_qio_required_fields_and_resource_validation(self):
        for field in ('no_data', 'emitting'):
            for value in (None, [], '../escape', 'bad space:model', 'fixturedata:absent'):
                model = {**self.wrapper, field: value}
                with self.assertRaises(mod.ImportError):
                    self.compiler().compile_model({'model': model})
        model = dict(self.wrapper)
        del model['emitting']
        with self.assertRaises(mod.ImportError):
            self.compiler().compile_model({'model': model})

    def test_qio_geometry_ownership_and_supplied_resource(self):
        self.put('fixturedata:child', {'parent': 'fixturedata:qio', 'no_data': 'fixturedata:qio_emitting'})
        compiler = self.compiler()
        result = compiler.compile_model({'model': 'fixturedata:child'})
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixturedata:online')
        own = {'parent': 'fixturedata:qio', 'loader': 'mekanism:data_based',
               'no_data': 'fixturedata:qio_emitting', 'emitting': 'fixturedata:qio_online'}
        result = compiler.compile_model({'model': own})
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixturedata:emitting')
        own.pop('emitting')
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model': own})
        supplied = compiler.assets.json('assets/fixturedata/models/qio_online.json')
        supplied['elements'][0]['to'][0] = 9
        self.put('fixturedata:qio_online', supplied)
        self.assertEqual(self.compiler().compile_model({'model': 'fixturedata:qio'})['elements'][0]['coordinates'][3], 9)

    def test_qio_cycle_and_child_transform(self):
        self.put('fixturedata:qio_online', {'loader': 'mekanism:data_based',
                                          'no_data': 'fixturedata:qio', 'emitting': 'fixturedata:qio_emitting'})
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compiler().compile_model({'model': 'fixturedata:qio'})
        self.put('fixturedata:qio_online', {'parent': 'fixturedata:qio_emitting', 'transform': {'translation': [1, 0, 0]}})
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compiler().compile_model({'model': 'fixturedata:qio'})

    def test_portable_full_and_partial_states(self):
        compiler = self.compiler()
        rows = compiler.block('refinedstorage:portable_grid', compiler.assets.json('assets/refinedstorage/blockstates/portable_grid.json'))
        self.assertEqual(len(rows), 39)
        self.assertFalse(compiler.unsupported_states)
        for name, _, _, models in rows:
            selector = dict(pair.split('=') for pair in name.partition('[')[2].rstrip(']').split(',') if pair)
            expected = 'fixturedata:active' if selector.get('active') == 'true' else 'fixturedata:inactive'
            self.assertEqual(models[0]['elements'][0]['faces'][0][-2], expected)
            self.assertTrue(models[0]['fixed_rotation'])
        self.assertTrue(any('empty-disk' in warning for warning in compiler.warnings))

    def test_portable_palette_rotation_replaces_application_but_keeps_uvlock(self):
        compiler = self.compiler()
        directions = {'north': (0, 0), 'east': (0, 90), 'south': (0, 180), 'west': (0, 270),
                      'up_north': (270, 180), 'up_east': (270, 270), 'up_south': (270, 0), 'up_west': (270, 90),
                      'down_north': (90, 0), 'down_east': (90, 90), 'down_south': (90, 180), 'down_west': (90, 270)}
        for facing, turn in directions.items():
            model = compiler.compile_model({'model': 'fixturedata:portable', 'x': 180, 'y': 270, 'uvlock': True},
                                           selector={'active': 'true', 'facing': facing})
            self.assertEqual(model['rotation'], (*turn, 1))
        self.assertEqual(compiler.compile_model({'model': 'fixturedata:portable'})['rotation'], (0, 0, 0))

    def test_portable_invalid_nested_and_multipart(self):
        compiler = self.compiler()
        for state in ({'active': 'yes'}, {'facing': 'up'}):
            with self.assertRaises(mod.ImportError):
                compiler.compile_model({'model': 'fixturedata:portable'}, selector=state)
        composite = {'loader': 'neoforge:composite', 'children': {'base': {'parent': 'fixturedata:portable'}}}
        with self.assertRaisesRegex(mod.ImportError, 'explicit'):
            compiler.compile_model({'model': composite})
        result = compiler.compile_model({'model': composite}, selector={'facing': 'north', 'active': 'true'})
        self.assertTrue(result['fixed_rotation'])
        with self.assertRaisesRegex(mod.ImportError, 'rotation'):
            compiler.compile_model({'model': composite, 'y': 90}, selector={'facing': 'east', 'active': 'true'})
        for facing in ('east', 'up_north'):
            with self.assertRaisesRegex(mod.ImportError, 'rotation'):
                compiler.compile_model({'model': composite}, selector={'facing': facing, 'active': 'true'})
        wrapper = {'loader': 'sophisticatedstorage:simple_material', 'base': {'parent': 'fixturedata:portable'}}
        nested = {'loader': 'neoforge:composite', 'children': {'base': wrapper}}
        with self.assertRaisesRegex(mod.ImportError, 'rotation'):
            compiler.compile_model({'model': nested}, selector={'facing': 'east', 'active': 'true'})
        with self.assertRaisesRegex(mod.ImportError, 'multipart'):
            compiler.block('fixturedata:multipart', {'multipart': [{'apply': {'model': 'fixturedata:portable'}}]})

    def test_portable_expansion_bounds_overlap_and_unrelated_variants(self):
        compiler = self.compiler()
        expand = mod._storage_module.expand_portable_grid
        unrelated = {'variants': {'other=a|b': {'model': 'fixturedata:qio_online'}}}
        self.assertIs(expand(compiler, 'fixturedata:other', unrelated, mod.ImportError), unrelated)
        excessive = {'variants': {f'slot={i}': {'model': 'fixturedata:portable'} for i in range(106)}}
        with self.assertRaisesRegex(mod.ImportError, '4096'):
            expand(compiler, 'fixturedata:large', excessive, mod.ImportError)
        overlap = {'variants': {'': {'model': 'fixturedata:portable'},
                                'facing=east': {'model': 'fixturedata:qio_online'}}}
        with self.assertRaisesRegex(mod.ImportError, 'overlapping'):
            expand(compiler, 'fixturedata:overlap', overlap, mod.ImportError)

    def test_portable_source_override_and_explicit_selector(self):
        compiler = self.compiler()
        source = compiler.assets.json('assets/refinedstorage/models/block/portable_grid/active.json')
        source['elements'][0]['from'][0] = 5
        self.put('refinedstorage:block/portable_grid/active', source)
        compiler = self.compiler()
        rows = compiler.block('fixturedata:restricted', {'variants': {'active=true,facing=east': {'model': 'fixturedata:portable'}}})
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][3][0]['elements'][0]['coordinates'][0], 5)
        self.assertEqual(rows[0][3][0]['rotation'], (0, 90, 0))
        source['transform'] = {'translation': [1, 0, 0]}
        self.put('refinedstorage:block/portable_grid/active', source)
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compiler().compile_model({'model': 'fixturedata:portable'}, selector={'active': 'true'})


if __name__ == '__main__':
    unittest.main()
