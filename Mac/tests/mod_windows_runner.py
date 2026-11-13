"""Exercise the real CLI path that avoids duplicate synthetic CI coverage."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from mod_assets import mod, put


class WindowsRunnerCLI(unittest.TestCase):
    def test_pack_only_writes_external_scripts_and_requires_pack(self):
        runner = Path(__file__).resolve().parents[2] / 'tools' / 'test_mod_windows.py'
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, pack, output = root / 'source', root / 'pack', root / 'output'
            put(source, 'assets/fixture/textures/block/runner.png',
                mod.encode_png(1, 1, bytes((30, 60, 90, 255))))
            put(source, 'assets/fixture/models/block/runner.json', {'elements': [{
                'from': [0, 0, 0], 'to': [16, 16, 16],
                'faces': {direction: {'texture': 'fixture:block/runner'} for direction in mod.DIRECTIONS}}]})
            put(source, 'assets/fixture/blockstates/runner_cube.json',
                {'variants': {'': {'model': 'fixture:block/runner'}}})
            mod.compile_pack([source], pack)
            arguments = [sys.executable, str(runner), '--fixtures-only', '--pack-only']
            result = subprocess.run([*arguments, '--pack', str(pack), '--output', str(output)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: external pack fixtures', result.stdout)
            self.assertEqual({path.name for path in output.iterdir()}, {
                'actual-input.schem', 'selected-states.txt', 'actual.mwscript', 'actual_roundtrip.mwscript'})
            self.assertEqual((output / 'selected-states.txt').read_text(), 'fixture:runner_cube\n')
            self.assertTrue((output / 'actual-input.schem').read_bytes())
            for name, world, exports in (('actual', 'actual-input.schem', 2),
                                         ('actual_roundtrip', 'actual.schem', 1)):
                script = (output / (name + '.mwscript')).read_text()
                self.assertIn('Mod pack: ' + str(pack / 'manifest.mwpack'), script)
                self.assertIn('Minecraft world: ' + str(output / world), script)
                self.assertEqual(script.count('Export for Rendering:'), exports)
                self.assertLess(script.index('Minecraft world:'), script.index('Selection location min to max:'))
            self.assertIn('Export Schematic: ' + str(output / 'actual.schem'),
                          (output / 'actual.mwscript').read_text())
            rejected = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('--pack-only requires --pack', rejected.stderr)


if __name__ == '__main__':
    unittest.main()
