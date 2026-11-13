"""Original assets exercise Sophisticated Storage's inspected no-material path."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_storage import storage
from mod_storage_fixtures import original_model, simple_material_assets




class SimpleStorageModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = simple_material_assets(self.root, mod.encode_png)

    def compile(self, source=None, rotation=(0, 0, 0), depth=0, paths=None):
        compiler = mod.Compiler(mod.Assets(paths or [self.root]))
        result = storage.compile_simple_material(compiler, 'fixture:simple_storage',
            self.source if source is None else source, rotation, {'facing': 'north'}, depth,
            mod.ImportError, 'fixture:simple_storage')
        return compiler, result

    def test_block_compilation_dispatches_supplied_default_base(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        definition = compiler.assets.json('assets/fixture/blockstates/simple_storage.json')
        rows = compiler.block('fixture:simple_storage', definition)
        self.assertEqual(len(rows), 2)
        self.assertFalse(compiler.unsupported_states)
        for name, _, _, models in rows:
            self.assertEqual(len(models), 1)
            self.assertEqual(len(models[0]['elements']), 2)
            expected = (90, 270, 1) if 'facing=east' in name else (0, 0, 0)
            self.assertEqual(models[0]['rotation'], expected)
        self.assertEqual(set(compiler.textures), {'fixture:base'})

    def test_exact_base_faces_uvs_and_element_rotation(self):
        compiler, result = self.compile()
        expected = compiler.compile_model({'model': self.source['base']})
        self.assertEqual(result, expected)
        self.assertEqual(len(result['elements']), 2)
        self.assertEqual(result['elements'][0]['coordinates'][:6], (2, 1, 3, 13, 14, 15))
        self.assertEqual(result['elements'][0]['faces'][0][:6], ('north', 2, 3, 11, 12, 90))
        self.assertEqual(result['elements'][1]['coordinates'][6:], ('z', 22.5, 4, 5, 6, 1))
        self.assertEqual(set(compiler.textures), {'fixture:base'})
        self.assertTrue(any('custom block-entity materials and their overlays' in w for w in compiler.warnings))

    def test_blockstate_rotation_and_uvlock_are_preserved(self):
        for rotation in [(90, 270, 1), (0, 0, 0, 90)]:
            compiler, result = self.compile(rotation=rotation)
            self.assertEqual(result['rotation'], rotation)

    def test_absent_material_does_not_load_overlay_or_outer_textures(self):
        source = copy.deepcopy(self.source)
        source['overlay'] = {'parent': 'fixture:missing_overlay'}
        source['textures'] = {'surface': 'fixture:missing_outer'}
        compiler, result = self.compile(source)
        self.assertEqual(len(result['elements']), 2)
        self.assertEqual(set(compiler.textures), {'fixture:base'})
        source.pop('overlay')
        self.assertEqual(self.compile(source)[1], result)

    def test_resource_override_replaces_parent_geometry_and_texture(self):
        override = self.root / 'overrides'
        replacement = original_model('fixture:overlay')
        replacement['elements'][0]['from'] = [1, 2, 3]
        put(override, 'assets/fixture/models/storage_parent.json', replacement)
        compiler, result = self.compile(paths=[self.root, override])
        self.assertEqual(result['elements'][0]['coordinates'][:3], (1, 2, 3))
        self.assertEqual(set(compiler.textures), {'fixture:overlay'})

    def test_bad_base_overlay_and_unrepresentable_root_transform_fail(self):
        for replacement in [None, [], 'fixture:storage_parent']:
            with self.subTest(base=replacement), self.assertRaises(mod.ImportError):
                self.compile({**self.source, 'base': replacement})
        with self.assertRaises(mod.ImportError):
            self.compile({**self.source, 'overlay': []})
        for target in ('outer', 'base'):
            source = copy.deepcopy(self.source)
            owner = source if target == 'outer' else source['base']
            owner['transform'] = {'translation': [1, 0, 0]}
            with self.subTest(target=target), self.assertRaisesRegex(mod.ImportError, 'root transforms'):
                self.compile(source)

    def test_missing_parent_cycles_and_depth_fail(self):
        with self.assertRaisesRegex(mod.ImportError, 'missing model'):
            self.compile({**self.source, 'base': {'parent': 'fixture:absent'}})
        put(self.root, 'assets/fixture/models/cycle.json', {'parent': 'fixture:cycle'})
        with self.assertRaisesRegex(mod.ImportError, 'cyclic'):
            self.compile({**self.source, 'base': {'parent': 'fixture:cycle'}})
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compile(depth=63)


if __name__ == '__main__':
    unittest.main()
