"""Supplementaries awning rotations, verified against AwningModelLoader."""
import copy
import tempfile
import unittest
from pathlib import Path

from mod_assets import mod, put, simple


class ExtraRotationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        simple(self.root)
        self.assets = mod.Assets([self.root])
        self.addCleanup(self.assets.close)
        self.compiler = mod.Compiler(self.assets)
        self.model = {'loader': 'supplementaries:extra_rotation', 'elements': [{
            'from': [0, 8, -1.088], 'to': [16, 8, 16], 'extra_rotation': 20.5560452196,
            'rotation': {'axis': 'x', 'angle': 0, 'origin': [0, 8, 16]},
            'faces': {'up': {'texture': 'sample:block/stone', 'uv': [0, 0, 16, 16]}}}]}

    def test_subtraction_preserves_origin_uv_and_application_turn(self):
        original = copy.deepcopy(self.model)
        result = self.compiler.compile_model({'model': self.model, 'y': 90})
        self.assertEqual(result['rotation'], (0, 90, 0))
        self.assertEqual(result['elements'][0]['coordinates'][6:], ('x', -20.5560452196, 0, 8, 16, 0))
        self.assertEqual(result['elements'][0]['faces'][0][1:5], (0, 0, 16, 16))
        self.assertEqual(self.model, original)
        self.model['elements'][0]['rotation']['angle'] = 22.5
        self.assertAlmostEqual(self.compiler.compile_model({'model': self.model})['elements'][0]['coordinates'][7],
                               22.5 - 20.5560452196)

    def test_parent_cache_keeps_texture_overrides_independent(self):
        put(self.root, 'assets/sample/models/block/awning.json', self.model)
        for color in ('red', 'blue'):
            put(self.root, f'assets/sample/textures/block/{color}.png', mod.encode_png(1, 1, bytes((10, 20, 30, 255))))
        self.model['elements'][0]['faces']['up']['texture'] = '#canvas'
        put(self.root, 'assets/sample/models/block/awning.json', self.model)
        self.assets.close()
        self.assets = mod.Assets([self.root])
        self.addCleanup(self.assets.close)
        self.compiler = mod.Compiler(self.assets)
        for color in ('red', 'blue', 'red'):
            result = self.compiler.compile_model({'model': {'parent': 'sample:block/awning',
                                                          'textures': {'canvas': 'sample:block/' + color}}})
            self.assertEqual(result['elements'][0]['faces'][0][-2], 'sample:block/' + color)
            self.assertAlmostEqual(result['elements'][0]['coordinates'][7], -20.5560452196)

    def test_invalid_rotation_contracts(self):
        for extra, rotation in ((float('nan'), {'axis': 'x'}), (True, {'axis': 'x'}),
                                (90, {'axis': 'x'}), (10, {}), (10, None),
                                (20, {'axis': 'x', 'rescale': True})):
            with self.subTest(extra=extra, rotation=rotation):
                model = copy.deepcopy(self.model)
                model['elements'][0].update(extra_rotation=extra, rotation=rotation)
                with self.assertRaises(mod.ImportError):
                    self.compiler.compile_model({'model': model})


if __name__ == '__main__':
    unittest.main()
