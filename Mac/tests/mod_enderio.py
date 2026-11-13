"""EnderIO's no-data overlay is empty while its machine composite remains visible."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_enderio_fixtures import enderio_assets

spec = importlib.util.spec_from_file_location('mod_enderio_models', Path(__file__).resolve().parents[2] / 'tools/mod_enderio_models.py')
enderio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(enderio)


class EnderIOModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent = enderio_assets(self.root, mod.encode_png)

    def compiler(self, paths=None):
        return mod.Compiler(mod.Assets(paths or [self.root]))

    def test_no_data_component_has_no_geometry_or_texture_dependencies(self):
        compiler = self.compiler()
        result = enderio.compile_io_overlay(compiler, 'fixture:io_overlay', (90, 270, 1))
        self.assertEqual(result, {'rotation': (90, 270, 1), 'elements': [], 'invisible': True})
        self.assertFalse(compiler.textures)
        self.assertTrue(any('require block-entity data' in w for w in compiler.warnings))

    def test_composite_retains_exact_base_geometry_and_variant_rotation(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:io_machine', compiler.assets.json('assets/fixture/blockstates/io_machine.json'))
        self.assertEqual(len(rows), 2)
        self.assertFalse(compiler.unsupported_states)
        expected = compiler.compile_model({'model': 'fixture:io_machine_base'})['elements']
        for name, _, _, models in rows:
            self.assertEqual(len(models), 1)
            self.assertFalse(models[0].get('invisible'))
            self.assertEqual(models[0]['elements'], expected)
            self.assertEqual(len(models[0]['elements'][0]['faces']), 3)
            self.assertEqual(models[0]['rotation'], (0, 90, 1) if 'east' in name else (0, 0, 0))
        self.assertEqual(set(compiler.textures), {'fixture:io_machine'})

    def test_overlay_identity_survives_as_explicit_invisible_state(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:io_overlay_only', compiler.assets.json('assets/fixture/blockstates/io_overlay_only.json'))
        self.assertEqual(rows, [('fixture:io_overlay_only', [0, 0, 0], 0, [])])

    def test_resource_override_can_replace_overlay_with_visible_standard_geometry(self):
        override = self.root / 'override'
        put(override, 'assets/fixture/models/io_overlay.json', self.parent)
        compiler = self.compiler([self.root, override])
        result = compiler.compile_model({'model': 'fixture:io_machine'})
        self.assertEqual(len(result['elements']), 2)
        self.assertFalse(result['invisible'])
        self.assertFalse(any('IO configuration overlay' in w for w in compiler.warnings))

    def test_loader_ignores_supplied_geometry_and_missing_texture_fields(self):
        compiler = self.compiler()
        result = compiler.compile_model({'model': {'loader': enderio.IO_OVERLAY_LOADER,
            'textures': {'unused': 'fixture:missing'}, 'elements': self.parent['elements'],
            'transform': {'translation': [10, 0, 0]}}})
        self.assertTrue(result['invisible'])
        self.assertEqual(result['elements'], [])
        self.assertFalse(compiler.textures)

    def test_painted_and_conditional_loaders_remain_explicitly_unsupported(self):
        compiler = self.compiler()
        for loader in ('enderio:painted_block', 'endercore:modloaded'):
            with self.subTest(loader=loader), self.assertRaisesRegex(mod.ImportError, 'custom model loader'):
                compiler.compile_model({'model': {'loader': loader, 'reference': 'minecraft:dirt'}})


if __name__ == '__main__':
    unittest.main()
