"""Original asset fixtures for inspected storage bases, colors, and twelve directions."""
import importlib.util
import math
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put

SPEC = importlib.util.spec_from_file_location('mod_storage_models', Path(__file__).resolve().parents[2] / 'tools/mod_storage_models.py')
storage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(storage)


def original_model(texture):
    return {'textures': {'surface': texture}, 'elements': [
        {'from': [2, 1, 3], 'to': [13, 14, 15], 'faces': {
            'north': {'texture': '#surface', 'uv': [2, 3, 11, 12], 'rotation': 90, 'cullface': 'north'},
            'up': {'texture': '#surface', 'uv': [1, 4, 13, 15]}}},
        {'from': [3, 4, 5], 'to': [6, 7, 8], 'faces': {'east': {'texture': '#surface', 'uv': [4, 5, 7, 8]}}}]}


class StorageModels(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        put(self.root, 'assets/fixture/blockstates/dummy.json', {'variants': {'': {'model': 'fixture:unused'}}})
        for label, color in [('active', (255, 0, 0, 255)), ('inactive', (0, 0, 255, 255))]:
            put(self.root, f'assets/fixture/textures/{label}.png', mod.encode_png(2, 2, bytes(color) * 4))

    def source(self, name, value):
        namespace, path = name.split(':')
        put(self.root, f'assets/{namespace}/models/{path}.json', value)

    def interface(self, color='red', tier=None):
        prefix = f'cabletiers:block/{tier}_disk_interface/' if tier else 'refinedstorage:block/disk_interface/'
        loader = f'cabletiers:{tier}_disk_interface' if tier else 'refinedstorage:disk_interface'
        self.source(prefix + color, {'loader': loader, 'color': color, **({'tier': tier} if tier else {})})
        self.source(prefix + 'base_' + color, original_model('fixture:active'))
        self.source(prefix + 'inactive', original_model('fixture:inactive'))
        return {'variants': {'': {'model': prefix + color}}}

    def compile(self, definition, name='fixture:storage'):
        compiler = mod.Compiler(mod.Assets([self.root]))
        expanded = storage.expand_block(compiler, name, definition, mod.ImportError)
        return compiler, compiler.block(name, expanded)

    def test_all_colors_tiers_and_active_states_preserve_geometry(self):
        for tier in [None, *storage.TIERS]:
            for color in storage.COLORS:
                with self.subTest(tier=tier, color=color):
                    compiler, rows = self.compile(self.interface(color, tier))
                    self.assertEqual(len(rows), 25)
                    self.assertFalse(compiler.unsupported_states)
                    for selector, _, _, models in rows:
                        self.assertEqual(len(models), 1)
                        model = models[0]
                        self.assertEqual(len(model['elements']), 2)
                        self.assertEqual(model['elements'][0]['coordinates'][:6], (2, 1, 3, 13, 14, 15))
                        self.assertEqual(model['elements'][-1]['coordinates'][:6], (3, 4, 5, 6, 7, 8))
                        face = model['elements'][0]['faces'][0]
                        self.assertEqual(face[:6], ('north', 2, 3, 11, 12, 90))
                        self.assertEqual(face[-2], 'fixture:active' if 'active=true' in selector else 'fixture:inactive')
                    self.assertTrue(any('installed disks' in warning for warning in compiler.warnings))

    def test_twelve_directions_match_shipped_quaternions(self):
        compiler, rows = self.compile(self.interface())
        # BiDirection constructor arguments from Refined Storage 2.0.9 bytecode.
        source_angles = {'north': (0, 0, 0), 'east': (0, -90, 0), 'south': (0, 180, 0), 'west': (0, 90, 0),
                         'up_north': (90, 0, 180), 'up_east': (90, 0, -90), 'up_south': (90, 0, 0),
                         'up_west': (90, 0, 90), 'down_north': (-90, 0, 0), 'down_east': (-90, 0, -90),
                         'down_south': (-90, 0, 180), 'down_west': (-90, 0, 90)}
        def rotate(point, axis, angle):
            x, y, z = point
            c, s = round(math.cos(math.radians(angle))), round(math.sin(math.radians(angle)))
            return ((x, c*y-s*z, s*y+c*z), (c*x+s*z, y, -s*x+c*z), (c*x-s*y, s*x+c*y, z))[axis]
        for selector, _, _, models in rows:
            state = mod.properties(selector.partition('[')[2][:-1]) if '[' in selector else {}
            source = source_angles[state.get('direction', 'north')]
            expected = tuple(rotate(rotate(rotate(p, 2, source[2]), 1, source[1]), 0, source[0])
                             for p in ((1, 0, 0), (0, 1, 0), (0, 0, 1)))
            x, y, _ = models[0]['rotation']
            self.assertEqual(mod.quarter_turn_orientation(x, y), expected, selector)
        self.assertEqual(rows[0][3][0]['rotation'], (0, 0, 0))

    def test_disk_drive_base_and_extended_drive_transform(self):
        self.source('refinedstorage:block/disk_drive', {'loader': 'refinedstorage:disk_drive'})
        self.source('refinedstorage:block/disk_drive/base', original_model('fixture:inactive'))
        compiler, rows = self.compile({'variants': {'': {'model': 'refinedstorage:block/disk_drive'}}})
        self.assertEqual(len(rows), 13)
        self.assertTrue(all('active=' not in row[0] for row in rows))
        self.source('extendedae:block/ex_drive', {'loader': 'extendedae:ex_drive'})
        self.source('extendedae:block/extended_drive/extended_me_drive_base', original_model('fixture:active'))
        compiler, rows = self.compile({'variants': {'facing=east,spin=0': {'model': 'extendedae:block/ex_drive', 'y': 90}}})
        self.assertEqual(rows[0][3][0]['rotation'], (0, 90, 0))
        self.assertEqual(len(rows[0][3][0]['elements']), 2)
        self.assertTrue(any('cell chassis' in warning for warning in compiler.warnings))

    def test_missing_active_base_has_no_misleading_generic_fallback(self):
        definition = self.interface()
        (self.root / 'assets/refinedstorage/models/block/disk_interface/base_red.json').unlink()
        compiler, rows = self.compile(definition)
        self.assertEqual(len(rows), 12)
        self.assertTrue(all('active=false' in row[0] for row in rows))
        self.assertTrue(any('missing model' in failure['reason'] for failure in compiler.unsupported_states))

    def test_invalid_loader_metadata_and_state(self):
        definition = self.interface(tier='elite')
        model = 'cabletiers:block/elite_disk_interface/red'
        for changes in ({'color': 'unknown'}, {'tier': 'ultra'}, {'transform': {}}, {'visibility': {}}):
            self.source(model, {'loader': 'cabletiers:elite_disk_interface', 'color': 'red', 'tier': 'elite', **changes})
            with self.subTest(changes=changes), self.assertRaises(mod.ImportError):
                self.compile(definition)
        definition = self.interface()
        for selector in ('direction=sideways', 'active=maybe', 'active=true,active=false'):
            with self.subTest(selector=selector), self.assertRaises(mod.ImportError):
                self.compile({'variants': {selector: definition['variants']['']}})
        with self.assertRaisesRegex(mod.ImportError, 'application transforms'):
            self.compile({'variants': {'': {**definition['variants'][''], 'x': 90}}})

    def test_expansion_keeps_selector_budget(self):
        definition = self.interface()
        application = definition['variants']['']
        oversized = {'variants': {f'custom={i}': application for i in range(180)}}
        with self.assertRaisesRegex(mod.ImportError, 'exceeds 4096'):
            self.compile(oversized)

    def test_unrelated_models_and_explicit_states_stay_explicit(self):
        definition = self.interface()
        application = definition['variants']['']
        compiler, rows = self.compile({'variants': {'active=true,direction=down_west': application}})
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][3][0]['rotation'], (90, 270, 0))
        original = {'variants': {'': {'model': 'minecraft:block/stone'}}}
        compiler = mod.Compiler(mod.Assets([self.root]))
        self.assertIs(storage.expand_block(compiler, 'fixture:stone', original, mod.ImportError), original)
        self.source('refinedstorage:block/custom', {'loader': 'refinedstorage:unknown'})
        unknown = {'variants': {'': {'model': 'refinedstorage:block/custom'}}}
        self.assertIs(storage.expand_block(compiler, 'fixture:unknown', unknown, mod.ImportError), unknown)


if __name__ == '__main__':
    unittest.main()
