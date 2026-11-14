"""Check pinned bootstrap metadata and reject runtime caches outside the package."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import urlsplit

TOOLS = Path(__file__).resolve().parent


class LauncherTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'darwin', 'Uses the built-in macOS bootstrap tools')
    def test_moved_app_reads_signed_tools_and_writes_user_cache(self):
        with tempfile.TemporaryDirectory(prefix='Moved Mineways app ') as temporary:
            root = Path(temporary).resolve()
            tools = root / 'Applications/Mineways.app/Contents/Resources/tools'
            tools.mkdir(parents=True)
            for name in ('showcase_runtime_pins.json', 'prepare_modpack.command', 'run_mod_helper.command'):
                shutil.copy2(TOOLS / name, tools)
            home = root / 'user'
            cache = home / 'Library/Caches/Mineways'
            pins = json.loads((tools / 'showcase_runtime_pins.json').read_text())['runtimes']
            for arch in ('macos-arm64', 'macos-x86_64'):
                python = cache / '.showcase-runtime' / (arch + '-' + pins[arch]['sha256'][:12]) / 'python/bin/python3'
                python.parent.mkdir(parents=True)
                python.symlink_to(sys.executable)
            (tools / 'local_module.py').write_text('ready = True\n')
            (tools / 'prepare_modpack.py').write_text(
                'import local_module, sys\nfrom pathlib import Path\n'
                'assert local_module.ready and sys.argv[1:3] == ["--cache-root", str(Path.cwd())]\n'
                'Path(sys.argv[3]).write_text(str(Path.cwd()))\n')
            result = root / 'result'
            subprocess.run([str(tools / 'prepare_modpack.command'), str(result)], check=True,
                           env={**os.environ, 'HOME': str(home)}, timeout=10)
            self.assertEqual(result.read_text(), str(cache))
            self.assertFalse((tools / '__pycache__').exists())
            self.assertFalse((tools.parent / '.showcase-runtime').exists())

    def test_runtime_pins(self):
        pins = json.loads((TOOLS / 'showcase_runtime_pins.json').read_text())
        self.assertEqual(pins['format'], 1)
        self.assertEqual(set(pins['runtimes']), {'windows-x64', 'macos-arm64', 'macos-x86_64'})
        for pin in pins['runtimes'].values():
            self.assertEqual(urlsplit(pin['url']).scheme, 'https')
            self.assertIn(urlsplit(pin['url']).hostname, {'www.python.org', 'github.com'})
            self.assertRegex(pin['sha256'], r'^[0-9a-f]{64}$')
            self.assertGreater(pin['size'], 1024 * 1024)
            self.assertLess(pin['size'], 64 * 1024 * 1024)

    @unittest.skipUnless(sys.platform == 'darwin', 'Uses the built-in macOS bootstrap tools')
    def test_reject_runtime_directory_symlink(self):
        with tempfile.TemporaryDirectory(prefix='Mineways launcher with spaces ') as temporary:
            root = Path(temporary)
            package = root / 'package'
            (package / 'tools').mkdir(parents=True)
            outside = root / 'outside'
            outside.mkdir()
            (package / '.showcase-runtime').symlink_to(outside, target_is_directory=True)
            shutil.copy2(TOOLS / 'showcase_runtime_pins.json', package / 'tools/showcase_runtime_pins.json')
            shutil.copy2(TOOLS / 'run_mod_helper.command', package / 'tools/run_mod_helper.command')
            launcher = package / 'Open ATM10 Showcase.command'
            shutil.copy2(TOOLS / launcher.name, launcher)
            result = subprocess.run(['/bin/bash', str(launcher), '--prepare-only'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('must not be a symlink', result.stderr)
            self.assertEqual(list(outside.iterdir()), [])
            modpack = package / 'tools/prepare_modpack.command'
            shutil.copy2(TOOLS / modpack.name, modpack)
            result = subprocess.run(['/bin/bash', str(modpack), 'instance', 'world', 'result'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('must not be a symlink', result.stderr)
            self.assertEqual(list(outside.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
