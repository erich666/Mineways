#!/usr/bin/env python3
"""Exercise interrupted setup, offline reuse, corruption, and launch-script paths."""
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import prepare_atm10_showcase as setup


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='showcase with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        tools = self.root / 'tools'
        (tools / 'fixtures').mkdir(parents=True)
        (tools / 'fixtures/atm10-showcase-1.21.1.json').write_text('{}')
        (tools / 'import_mod_assets.py').write_text('# fixture')
        self.sources = self.root / 'sources.txt'
        self.sources.write_text('fixture.jar\n')
        self.download = Mock(return_value=self.sources)
        downloader = SimpleNamespace(read_manifest=lambda _: [{'size': 100}], download_manifest=self.download)
        self.compile = Mock(side_effect=self.compile_pack)
        importer = SimpleNamespace(compile_pack=self.compile)
        self.addCleanup(patch.stopall)
        patch.object(setup, 'helper', side_effect=lambda name: downloader if name == 'download_mod_assets' else importer).start()

    def compile_pack(self, sources, destination):
        self.assertEqual(sources, ['fixture.jar'])
        (destination / 'textures').mkdir(parents=True)
        (destination / 'manifest.mwpack').write_text('fixture manifest')
        (destination / 'textures/0000.png').write_bytes(b'fixture pixels')
        return {'blocks': 24}

    def test_completed_pack_reopens_offline_and_after_relocation(self):
        pack = setup.prepare(self.root)
        self.download.side_effect = AssertionError('An offline launch must not download')
        self.compile.side_effect = AssertionError('A repeat launch must not compile')
        self.assertEqual(setup.prepare(self.root), pack)
        moved = self.root / 'relocated'
        moved.mkdir()
        (self.root / 'tools').rename(moved / 'tools')
        pack.rename(moved / 'atm10-pack')
        self.assertEqual(setup.prepare(moved), moved / 'atm10-pack')

    def test_interrupted_compile_never_publishes_partial_pack(self):
        def fail(sources, destination):
            destination.mkdir()
            (destination / 'manifest.mwpack').write_text('partial')
            raise RuntimeError('interrupted')
        self.compile.side_effect = fail
        with self.assertRaisesRegex(RuntimeError, 'interrupted'):
            setup.prepare(self.root)
        self.assertFalse((self.root / 'atm10-pack').exists())
        self.assertFalse(list(self.root.glob('.showcase-prepare-*')))
        self.compile.side_effect = self.compile_pack
        self.assertTrue((setup.prepare(self.root) / 'manifest.mwpack').is_file())

    def test_failed_download_never_compiles(self):
        self.download.side_effect = ValueError('download checksum mismatch')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            setup.prepare(self.root)
        self.compile.assert_not_called()
        self.assertFalse((self.root / 'atm10-pack').exists())

    def test_corrupt_texture_is_detected_without_overwriting(self):
        pack = setup.prepare(self.root)
        texture = pack / 'textures/0000.png'
        texture.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'incomplete or changed'):
            setup.prepare(self.root)
        self.assertEqual(texture.read_bytes(), b'changed')
        self.assertEqual(self.compile.call_count, 1)

    def test_changed_importer_preserves_previous_pack(self):
        pack = setup.prepare(self.root)
        before = setup.inventory(pack)
        (self.root / 'tools/import_mod_assets.py').write_text('# changed importer')
        with self.assertRaisesRegex(ValueError, 'incomplete or changed'):
            setup.prepare(self.root)
        self.assertEqual(setup.inventory(pack), before)

    def test_launch_script_load_order_paths_and_platform_commands(self):
        showcase = self.root / 'atm10-showcase'
        showcase.mkdir()
        (showcase / 'catalog.json').write_text(json.dumps({
            'selection_min': [0, 64, 0], 'selection_max': [15, 67, 15], 'focus': [8, 66, 8]}))
        pack = self.root / 'atm10-pack'
        for windows in (False, True):
            lines = setup.write_launch_script(self.root, pack, windows).read_text().splitlines()
            mod = lines.index('Mod pack: ' + str(pack / 'manifest.mwpack'))
            world = lines.index('Minecraft world: ' + str(showcase / 'world'))
            selection = lines.index('Selection location min to max: 0, 64, 0 to 15, 67, 15')
            self.assertLess(mod, world)
            self.assertLess(world, selection)
            self.assertEqual('Show error: script' in lines, windows)
            self.assertNotIn('Close', lines)


if __name__ == '__main__':
    unittest.main()
