"""Minecraft treats unknown cullface names as faces without neighbor culling."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, simple


class CullfaceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-cullface-')
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        simple(root)
        self.assets = mod.Assets([root])
        self.addCleanup(self.assets.close)

    def compile(self, value, present=True):
        face = {'texture': 'sample:block/stone', 'uv': [1, 2, 14, 15]}
        if present:
            face['cullface'] = value
        compiler = mod.Compiler(self.assets)
        result = compiler.compile_model({'model': {'elements': [
            {'from': [2, 3, 4], 'to': [14, 13, 12], 'faces': {'down': face}}]}})
        return compiler, result

    def test_unknown_strings_preserve_geometry_and_disable_neighbor_culling(self):
        _, expected = self.compile(None, present=False)
        for name in ('bottom', 'DOWN', '', 'unknown', 'x' * 1023):
            with self.subTest(name=name[:20]):
                compiler, actual = self.compile(name)
                self.assertEqual(actual, expected)
                self.assertEqual(len(compiler.warnings), 1)
                warning = next(iter(compiler.warnings))
                self.assertIn('unknown cullface', warning)
                self.assertIn('disables neighbor culling', warning)
                self.assertTrue(warning.startswith('<inline model>:'))

    def test_known_directions_and_absent_cullface_keep_existing_behavior(self):
        for name in (*mod.DIRECTIONS, '-'):
            compiler, actual = self.compile(name)
            self.assertEqual(actual['elements'][0]['faces'][0][-1], name)
            self.assertFalse(compiler.warnings)
        compiler, actual = self.compile(None, present=False)
        self.assertEqual(actual['elements'][0]['faces'][0][-1], '-')
        self.assertFalse(compiler.warnings)

    def test_nonstring_and_excessive_values_remain_rejected(self):
        for value in (None, True, False, 0, 1.5, [], {}, ['down'], {'name': 'down'}, 'x' * 1024):
            with self.subTest(value=repr(value)[:30]), self.assertRaisesRegex(mod.ImportError, 'invalid cullface'):
                self.compile(value)


if __name__ == '__main__':
    unittest.main()
