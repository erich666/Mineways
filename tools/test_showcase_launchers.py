"""Check pinned bootstrap metadata and reject runtime caches outside the package."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import urlsplit

TOOLS = Path(__file__).resolve().parent


class LauncherTests(unittest.TestCase):
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
            launcher = package / 'Open ATM10 Showcase.command'
            shutil.copy2(TOOLS / launcher.name, launcher)
            result = subprocess.run(['/bin/bash', str(launcher), '--prepare-only'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('must not be a symlink', result.stderr)
            self.assertEqual(list(outside.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
