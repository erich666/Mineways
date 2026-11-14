"""Rectangular static textures and metadata-defined animation frames."""
import tempfile
import unittest
from pathlib import Path

from mod_assets import mod, put


class TextureShapes(unittest.TestCase):
    def texture(self, width, height, metadata=None):
        root = Path(self.temp.name)
        put(root, 'assets/sample/blockstates/shape.json', {'variants': {'': {'model': 'sample:block/shape'}}})
        pixels = bytes(channel for y in range(height) for x in range(width)
                       for channel in (x * 17, y * 23, 91, 255))
        name = 'assets/sample/textures/block/shape.png'
        put(root, name, mod.encode_png(width, height, pixels))
        if metadata is not None:
            put(root, name + '.mcmeta', metadata)
        compiler = mod.Compiler(mod.Assets([root]))
        compiler.texture('sample:block/shape')
        return mod.decode_png(compiler.textures['sample:block/shape'][0]), compiler.warnings

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def test_static_rectangles_preserve_entire_image(self):
        for width, height in ((4, 2), (2, 4), (3, 2)):
            with self.subTest(width=width, height=height):
                (w, h, pixels), warnings = self.texture(width, height)
                size = max(width, height)
                self.assertEqual((w, h), (size, size))
                for y in range(size):
                    for x in range(size):
                        self.assertEqual(pixels[(y * size + x) * 4:(y * size + x + 1) * 4],
                                         bytes((x * width // size * 17, y * height // size * 23, 91, 255)))
                self.assertFalse(any('animation' in warning for warning in warnings))

    def test_animation_honors_first_sequence_frame(self):
        (w, h, pixels), warnings = self.texture(2, 4, {'animation': {'frames': [1, 0]}})
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(pixels[:4], bytes((0, 46, 91, 255)))
        self.assertTrue(any('first frame' in warning for warning in warnings))

    def test_rectangular_animation_frame(self):
        (w, h, pixels), _ = self.texture(4, 4, {'animation': {'width': 4, 'height': 2, 'frames': [{'index': 1, 'time': 2}, 0]}})
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(pixels[:4], bytes((0, 46, 91, 255)))
        self.assertEqual(pixels[-4:], bytes((51, 69, 91, 255)))

    def test_singleton_sequence_uses_image_origin(self):
        for frame in (1, {'index': 1, 'time': 2}):
            (w, h, pixels), warnings = self.texture(4, 4, {
                'animation': {'width': 4, 'height': 2, 'frames': [frame]}})
            self.assertEqual((w, h), (4, 4))
            self.assertEqual(pixels[:4], bytes((0, 0, 91, 255)))
            self.assertEqual(pixels[-4:], bytes((51, 23, 91, 255)))
            self.assertTrue(any('singleton' in warning for warning in warnings))

    def test_width_only_and_height_only_defaults(self):
        cases = [({'width': 2}, 6, (34, 0, 91, 255), (51, 115, 91, 255)),
                 ({'height': 3}, 4, (0, 69, 91, 255), (51, 115, 91, 255))]
        for dimensions, size, first, last in cases:
            with self.subTest(dimensions=dimensions):
                (w, h, pixels), _ = self.texture(4, 6, {'animation': {**dimensions, 'frames': [1, 0]}})
                self.assertEqual((w, h), (size, size))
                self.assertEqual(pixels[:4], bytes(first))
                self.assertEqual(pixels[-4:], bytes(last))

    def test_unspecified_dimension_sentinels(self):
        for explicit, omitted in [({'width': -1, 'height': -1}, {}),
                                  ({'width': -1, 'height': 2}, {'height': 2}),
                                  ({'width': 2, 'height': -1}, {'width': 2})]:
            with self.subTest(explicit=explicit):
                expected, _ = self.texture(4, 8, {'animation': {**omitted, 'frames': [1, 0]}})
                actual, _ = self.texture(4, 8, {'animation': {**explicit, 'frames': [1, 0]}})
                self.assertEqual(actual, expected)
        (w, h, pixels), _ = self.texture(4, 8, {'animation': {}})
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(pixels[-4:], bytes((51, 69, 91, 255)))

    def test_silentgear_fractional_duration_shape(self):
        # Silent Gear 4.2.1.1 uses this 16x256 shape and metadata for belt_on.
        # These colors are original test data, not the mod's texture artwork.
        root = Path(self.temp.name)
        put(root, 'assets/silentgear/blockstates/refabricator.json',
            {'variants': {'lit=true': {'model': 'silentgear:block/refabricator'}}})
        name = 'assets/silentgear/textures/block/belt_on.png'
        pixels = bytes(channel for y in range(256) for x in range(16)
                       for channel in (x * 17, y, 91, 255))
        put(root, name, mod.encode_png(16, 256, pixels))
        put(root, name + '.mcmeta', {'animation': {'interpolate': False, 'frametime': 2.4}})
        compiler = mod.Compiler(mod.Assets([root]))
        compiler.texture('silentgear:block/belt_on')
        self.assertEqual(mod.decode_png(compiler.textures['silentgear:block/belt_on'][0]),
                         (16, 16, pixels[:16 * 16 * 4]))

    def test_fractional_sequence_durations(self):
        for duration in (1.0, 1.5, 2.4, 2147483647.5):
            with self.subTest(duration=duration):
                (w, h, pixels), _ = self.texture(2, 4, {'animation': {
                    'frametime': duration, 'frames': [{'index': 1, 'time': duration}, 0]}})
                self.assertEqual((w, h), (2, 2))
                self.assertEqual(pixels[:4], bytes((0, 46, 91, 255)))
        for duration in (0.9, -0.1, '2.4', True, None, 2147483648):
            with self.subTest(duration=duration), self.assertRaises(mod.ImportError):
                self.texture(2, 4, {'animation': {'frames': [{'index': 1, 'time': duration}, 0]}})

    def test_invalid_animation_metadata(self):
        for animation in ([], {'width': 0}, {'width': True}, {'height': 3}, {'frames': 'bad'},
                          {'frames': [-1]}, {'frames': [4]}, {'frames': [True]}, {'frames': [{}]},
                          {'width': -2}, {'height': None}, {'frametime': 0}, {'frametime': True},
                          {'frametime': 0.9}, {'frametime': '2.4'}, {'frametime': -2.4}, {'frametime': 2147483648}, {'frames': [None]},
                          {'frames': [0, 'bad']}, {'frames': [0, {'index': 0, 'time': 0}]},
                          {'frames': [{'index': 0, 'time': -1}]},
                          {'frames': [{'index': 0, 'time': True}]},
                          {'frames': [{'index': 0, 'time': 2147483648}]}):
            with self.subTest(animation=animation), self.assertRaises(mod.ImportError):
                self.texture(4, 4, {'animation': animation})
        with self.assertRaisesRegex(mod.ImportError, 'invalid Fusion texture metadata'):
            self.texture(4, 4, {'fusion': []})

    def test_normalized_pixel_budget(self):
        old = mod.MAX_PIXELS
        self.addCleanup(setattr, mod, 'MAX_PIXELS', old)
        mod.MAX_PIXELS = 8
        with self.assertRaisesRegex(mod.ImportError, 'normalization exceeds pixel limit'):
            self.texture(4, 2)


if __name__ == '__main__':
    unittest.main()
