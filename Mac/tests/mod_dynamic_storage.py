"""Placed storage defaults use source parts and palette flags, never proxy cubes."""
import copy
import importlib.util
import itertools
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod
from mod_storage import storage
from mod_dynamic_storage_fixtures import dynamic_storage_assets, put

spec = importlib.util.spec_from_file_location('backpack_adapter', Path(__file__).parents[2] / 'tools/mod_backpack_models.py')
backpacks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backpacks)


class DynamicStorage(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.barrel, self.backpack = dynamic_storage_assets(self.root, mod.encode_png)

    def compiler(self):
        return mod.Compiler(mod.Assets([self.root]))

    def barrel_model(self, state=None, reference='fixturestorage:barrel', rotation=(0, 0, 0), compiler=None):
        compiler = compiler or self.compiler()
        return storage.compile_barrel(compiler, reference, {}, rotation, state or {}, 0, mod.ImportError)

    def backpack_model(self, state=None, compiler=None, rotation=(0, 0, 0)):
        compiler = compiler or self.compiler()
        model = compiler.model('fixturestorage:backpack')
        return compiler, backpacks.compile_backpack(compiler, 'fixturestorage:backpack', model,
                                                    rotation, state or {}, 0, mod.ImportError)

    def test_barrel_open_flat_and_rotations(self):
        closed = self.barrel_model()
        opened = self.barrel_model({'open': 'true'})
        self.assertEqual(closed['elements'][0]['coordinates'][4], 14)
        self.assertEqual(opened['elements'][0]['coordinates'][4], 10)
        self.assertEqual(opened['elements'][0]['faces'][0][-2], 'fixturestorage:open')
        flat = self.barrel_model(reference='fixturestorage:barrel_flat', rotation=(270, 0, 0))
        self.assertEqual(flat['elements'][0]['coordinates'][4], 16)
        self.assertEqual(flat['rotation'], (270, 0, 0))
        for rotation in ((0, 0, 0), (0, 90, 0), (0, 180, 0), (0, 270, 0), (90, 0, 0), (270, 0, 0)):
            self.assertEqual(self.barrel_model(rotation=rotation)['rotation'], rotation)

    def test_barrel_missing_open_and_wrong_family_rejected(self):
        source = copy.deepcopy(self.barrel)
        del source['model_parts']['base_open']
        with self.assertRaisesRegex(mod.ImportError, 'base_open'):
            self.barrel_model({'open': 'true'}, source)
        compiler = self.compiler()
        self.barrel_model(compiler=compiler)  # Populate the cache before wrong-family lookup.
        with self.assertRaises(mod.ImportError):
            storage._limited_barrel_parts(compiler, 'fixturestorage:barrel', mod.ImportError)
        with self.assertRaises(mod.ImportError):
            self.barrel_model({'open': 'yes'})

    def test_barrel_loaderless_override_is_ignored(self):
        source = {'parent': 'fixturestorage:barrel', 'model_parts': {'base': {'model': 'fixturestorage:open'}}}
        self.assertEqual(self.barrel_model(reference=source)['elements'][0]['coordinates'][4], 14)
        source['loader'] = storage.BARREL_LOADER
        self.assertEqual(self.barrel_model(reference=source)['elements'][0]['coordinates'][4], 10)

    def test_backpack_all_palette_parts(self):
        compiler = self.compiler()
        cached = copy.deepcopy(compiler.model('sophisticatedbackpacks:block/backpack_base'))
        for left, right, battery in itertools.product(('false', 'true'), repeat=3):
            _, model = self.backpack_model(dict(left_tank=left, right_tank=right, battery=battery), compiler)
            self.assertEqual([e['coordinates'][0] for e in model['elements']],
                             [0, 5 if left == 'true' else 1, 6 if right == 'true' else 2,
                              7 if battery == 'true' else 3, 4])
        self.assertEqual(compiler.model('sophisticatedbackpacks:block/backpack_base'), cached)
        self.assertEqual(self.backpack_model(rotation=(90, 270, 1))[1]['rotation'], (90, 270, 1))
        self.assertTrue(any('fluids, charge' in warning for warning in compiler.warnings))

    def test_backpack_exact_colors_and_clips_scope(self):
        compiler, result = self.backpack_model()
        faces = result['elements'][0]['faces']
        expected = ((200, 76, 22, 192), (96, 36, 10, 192), (20, 180, 240, 255))
        for face, color in zip(faces, expected):
            self.assertEqual(mod.decode_png(compiler.textures[face[-2]][0])[2], bytes(color) * 4)
        self.assertNotIn('fixturestorage:unused', compiler.textures)
        self.assertEqual(len(result['elements']), 5)  # No fluid/charge/displayed-item meshes.

    def test_backpack_face_metadata_is_multiplied_once(self):
        path = 'assets/sophisticatedbackpacks/models/block/backpack_base.json'
        source = self.compiler().assets.json(path)
        source['elements'][0]['neoforge_data'] = {'color': '80ffffff'}
        source['elements'][0]['faces']['north']['neoforge_data'] = {'color': 'ff80ffff'}
        put(self.root, path, source)
        compiler, result = self.backpack_model()
        faces = result['elements'][0]['faces']
        self.assertEqual(mod.decode_png(compiler.textures[faces[0][-2]][0])[2][:4], bytes((100, 76, 22, 192)))
        self.assertEqual(mod.decode_png(compiler.textures[faces[1][-2]][0])[2][:4], bytes((96, 36, 10, 96)))

    def test_backpack_override_and_invalid_parts(self):
        put(self.root, 'assets/fixturestorage/models/backpack.json', {
            'parent': 'fixturestorage:backpack_parent', 'textures': {'tier': 'fixturestorage:tier'}})
        put(self.root, 'assets/fixturestorage/models/backpack_parent.json', self.backpack)
        compiler, result = self.backpack_model()
        self.assertEqual(result['elements'][0]['faces'][2][-2], 'fixturestorage:tier')
        path = 'assets/sophisticatedbackpacks/models/block/backpack_base.json'
        source = compiler.assets.json(path)
        source['transform'] = {'translation': [1, 0, 0]}
        put(self.root, path, source)
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.backpack_model()
        with self.assertRaises(mod.ImportError):
            self.backpack_model({'battery': 'perhaps'})

    def test_backpack_missing_clips_and_cycles(self):
        for textures in ({}, {'clips': '#absent'}, {'clips': 'minecraft:missingno'}):
            put(self.root, 'assets/fixturestorage/models/backpack.json',
                {'loader': backpacks.LOADER, 'textures': textures})
            _, result = self.backpack_model()
            self.assertEqual(result['elements'][0]['faces'][2][-2], 'fixturestorage:unused')
        put(self.root, 'assets/fixturestorage/models/backpack.json',
            {'loader': backpacks.LOADER, 'textures': {'clips': '#other', 'other': '#clips'}})
        _, result = self.backpack_model()
        self.assertEqual(result['elements'][0]['faces'][2][-2], 'fixturestorage:unused')
        path = 'assets/sophisticatedbackpacks/models/block/backpack_base.json'
        source = self.compiler().assets.json(path)
        source['textures']['cloth'] = '#loop'
        source['textures']['loop'] = '#cloth'
        put(self.root, path, source)
        with self.assertRaisesRegex(mod.ImportError, 'cyclic backpack part'):
            self.backpack_model()

    def test_backpack_supplied_part_override_and_texture_budget(self):
        path = 'assets/sophisticatedbackpacks/models/block/backpack_base.json'
        source = self.compiler().assets.json(path)
        source['elements'][0]['to'][1] = 9
        put(self.root, path, source)
        compiler, result = self.backpack_model()
        self.assertEqual(result['elements'][0]['coordinates'][4], 9)
        compiler = self.compiler()
        compiler.face_texture_pixel_count = mod._obj_module.MAX_GENERATED_PIXELS
        with self.assertRaisesRegex(mod.ImportError, 'pixel budget'):
            self.backpack_model(compiler=compiler)

    def test_explicit_flags_are_not_replaced(self):
        compiler = self.compiler()
        definition = {'variants': {'battery=true,left_tank=false,right_tank=true':
            {'model': 'fixturestorage:backpack', 'y': 90}}}
        expanded = storage.expand_loader_flags(compiler, 'fixturestorage:backpack', definition,
            backpacks.LOADER, ('left_tank', 'right_tank', 'battery'), mod.ImportError)
        self.assertEqual(expanded, definition)
        rows = compiler.block('fixturestorage:backpack', definition)
        self.assertEqual(len(rows), 1)
        self.assertEqual([e['coordinates'][0] for e in rows[0][3][0]['elements']], [0, 1, 6, 7, 4])

    def test_block_dispatch_expands_and_keeps_defaults(self):
        compiler = self.compiler()
        for block, count in [('barrel', 9), ('backpack', 30)]:
            definition = compiler.assets.json(f'assets/fixturestorage/blockstates/{block}.json')
            rows = compiler.block('fixturestorage:' + block, definition)
            self.assertFalse(compiler.unsupported_states)
            self.assertEqual(len(rows), count)
            self.assertTrue(any(not any(flag in row[0] for flag in ('open=', 'battery=')) for row in rows))

    def test_nested_dynamic_models_require_all_palette_flags(self):
        force = {'loader': 'twilightforest:force_field', 'textures': {'surface': 'fixturestorage:wood'},
                 'elements': [{'from': [7, 0, 7], 'to': [9, 16, 9],
                               'faces': {'north': {'texture': '#surface'}}}]}
        cases = [({'parent': 'fixturestorage:barrel'}, {'open': 'true'}, 2),
                 ({'parent': 'fixturestorage:backpack'},
                  {'left_tank': 'true', 'right_tank': 'false', 'battery': 'true'}, 5),
                 (force, {side: 'false' for side in mod._twilight_module.DIRECTIONS}, 1)]
        for model, flags, expected_elements in cases:
            for nesting in (1, 2):
                nested = model
                for _ in range(nesting):
                    nested = {'loader': 'neoforge:composite', 'children': {'storage': nested}}
                for missing in (None, {}, {key: value for key, value in flags.items() if key != next(iter(flags))}):
                    with self.subTest(model=model, nesting=nesting, missing=missing), self.assertRaisesRegex(
                            mod.ImportError, 'nested dynamic model requires explicit palette flags'):
                        self.compiler().compile_model({'model': nested}, selector=missing)
                result = self.compiler().compile_model({'model': nested}, selector=flags)
                self.assertEqual(len(result['elements']), expected_elements)
                # The direct root still has the verified default for missing properties.
                direct = self.compiler().compile_model({'model': model})
                self.assertEqual(len(direct['elements']), expected_elements)

    def test_delegating_wrapper_requires_explicit_palette_flags(self):
        wrapper = {'loader': 'domum_ornamentum:materially_textured', 'parent': 'fixturestorage:barrel'}
        for state in (None, {}, {'facing': 'north'}):
            with self.assertRaisesRegex(mod.ImportError, 'nested dynamic model requires explicit palette flags'):
                self.compiler().compile_model({'model': wrapper}, selector=state)
        result = self.compiler().compile_model({'model': wrapper}, selector={'open': 'true'})
        self.assertEqual(result['elements'][0]['coordinates'][4], 10)

    def test_partial_palette_preserves_explicit_true_and_false_flags(self):
        compiler = self.compiler()
        definition = compiler.assets.json('assets/fixturestorage/blockstates/backpack.json')
        rows = compiler.block('fixturestorage:backpack', definition)
        parsed = [(mod.properties(row[0].partition('[')[2][:-1]), row) for row in rows]
        for values in itertools.product((None, 'false', 'true'), repeat=3):
            flags = dict(zip(('left_tank', 'right_tank', 'battery'), values))
            observed = {'facing': 'north', **{key: value for key, value in flags.items() if value is not None}}
            # Mirror native ResolveModState's subset predicate and specificity.
            matches = [(len(props), row) for props, row in parsed
                       if all(observed.get(key) == value for key, value in props.items())]
            self.assertTrue(matches)
            selected = max(matches, key=lambda item: item[0])[1]
            self.assertEqual([e['coordinates'][0] for e in selected[3][0]['elements']],
                             [0, 5 if flags['left_tank'] == 'true' else 1,
                              6 if flags['right_tank'] == 'true' else 2,
                              7 if flags['battery'] == 'true' else 3, 4])
        self.assertEqual(len(rows), 30)

    def test_partial_force_field_uses_only_observed_true_directions(self):
        def element(x, direction=None):
            value = {'from': [x, 0, 0], 'to': [x + 1, 1, 1],
                     'faces': {'north': {'texture': 'fixturestorage:wood'}}}
            if direction:
                value['condition'] = {'direction': direction, 'parents': []}
            return value
        directions = mod._twilight_module.DIRECTIONS
        model = {'loader': 'twilightforest:force_field',
                 'elements': [element(0)] + [element(i + 1, side) for i, side in enumerate(directions)]}
        compiler = self.compiler()
        rows = compiler.block('fixturestorage:field', {'variants': {'': {'model': model}}})
        self.assertEqual(len(rows), 127)
        parsed = [(mod.properties(row[0].partition('[')[2][:-1]), row) for row in rows]
        for observed in ({}, {'north': 'true'}, {'north': 'true', 'east': 'false'},
                         {'up': 'false', 'south': 'true', 'west': 'true'},
                         {side: 'false' for side in directions}):
            matches = [(len(props), row) for props, row in parsed
                       if all(observed.get(key) == value for key, value in props.items())]
            selected = max(matches, key=lambda item: item[0])[1]
            self.assertEqual([e['coordinates'][0] for e in selected[3][0]['elements']],
                             [0] + [i + 1 for i, side in enumerate(directions) if observed.get(side) == 'true'])

    def test_expansion_bounds_and_invalid_flags(self):
        compiler = self.compiler()
        application = {'model': 'fixturestorage:backpack'}
        for definition in ({'variants': {'battery=2': application}},
                           {'multipart': [{'apply': application}]},
                           {'variants': {f'other={i}': application for i in range(600)}}):
            with self.assertRaises(mod.ImportError):
                storage.expand_loader_flags(compiler, 'fixturestorage:backpack', definition,
                    backpacks.LOADER, ('left_tank', 'right_tank', 'battery'), mod.ImportError)


if __name__ == '__main__':
    unittest.main()
