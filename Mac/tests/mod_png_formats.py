"""Original PNG fixtures for Adam7, 16-bit samples, and malformed image bounds."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import zlib

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/import_mod_assets.py'
spec = importlib.util.spec_from_file_location('import_mod_assets', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def image(width, height, depth, color, pixels, interlace=0, palette=b'', transparency=b'', raw_override=None):
    """Encode independent test rows with all five PNG filters, including narrow passes."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    # The Adam7 8x8 tile labels independently select each pass's pixel positions.
    labels = ((1, 6, 4, 6, 2, 6, 4, 6), (7, 7, 7, 7, 7, 7, 7, 7),
              (5, 6, 5, 6, 5, 6, 5, 6), (7, 7, 7, 7, 7, 7, 7, 7),
              (3, 6, 4, 6, 3, 6, 4, 6), (7, 7, 7, 7, 7, 7, 7, 7),
              (5, 6, 5, 6, 5, 6, 5, 6), (7, 7, 7, 7, 7, 7, 7, 7))
    raw = bytearray()
    row_count = 0
    for pass_number in range(1, 8) if interlace else [0]:
        previous = b''
        for y in range(height):
            selected = [pixels[y * width + x] for x in range(width)
                        if not interlace or labels[y % 8][x % 8] == pass_number]
            if not selected:
                continue
            samples = [value for pixel in selected for value in pixel]
            if depth == 16:
                row = struct.pack('>' + 'H' * len(samples), *samples)
            elif depth == 8:
                row = bytes(samples)
            else:
                packed = bytearray((len(samples) * depth + 7) // 8)
                for index, sample in enumerate(samples):
                    packed[index * depth // 8] |= sample << (8 - depth - index * depth % 8)
                row = bytes(packed)
            previous = previous or bytes(len(row))
            bpp = max(1, channels * depth // 8)
            kind = row_count % 5
            raw.append(kind)
            for index, sample in enumerate(row):
                left = row[index - bpp] if index >= bpp else 0
                up = previous[index]
                corner = previous[index - bpp] if index >= bpp else 0
                if kind == 0:
                    predictor = 0
                elif kind == 1:
                    predictor = left
                elif kind == 2:
                    predictor = up
                elif kind == 3:
                    predictor = (left + up) // 2
                else:
                    prediction = left + up - corner
                    predictor = min((left, up, corner), key=lambda value: abs(prediction - value))
                raw.append((sample - predictor) & 255)
            previous = row
            row_count += 1
    if raw_override is not None:
        raw = raw_override
    result = b'\x89PNG\r\n\x1a\n'
    result += mod.png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, interlace))
    if palette:
        result += mod.png_chunk(b'PLTE', palette)
    if transparency:
        result += mod.png_chunk(b'tRNS', transparency)
    result += mod.png_chunk(b'IDAT', zlib.compress(raw)) + mod.png_chunk(b'IEND', b'')
    return result


class PNGFormats(unittest.TestCase):
    def test_all_standard_formats_and_pass_edges(self):
        for color, depths in {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}.items():
            channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
            for depth in depths:
                maximum = (1 << depth) - 1
                palette = bytes(value for i in range(maximum + 1) for value in (i, 255 - i, i ^ 91)) if color == 3 else b''
                transparency = bytes(range(maximum + 1)) if color == 3 else b''
                for width, height in [(1, 1), (1, 9), (9, 1), (3, 5), (9, 11)]:
                    pixels = [tuple((i * 179 + channel * 593 + 131) & maximum for channel in range(channels))
                              for i in range(width * height)]
                    expected = bytearray()
                    for samples in pixels:
                        rgba = [sample >> 8 if depth == 16 else sample for sample in samples]
                        if color == 0:
                            gray = rgba[0] if depth == 16 else rgba[0] * 255 // maximum
                            expected.extend((gray, gray, gray, 255))
                        elif color == 2:
                            expected.extend((*rgba, 255))
                        elif color == 3:
                            i = samples[0]
                            expected.extend((*palette[i * 3:i * 3 + 3], transparency[i]))
                        elif color == 4:
                            expected.extend((rgba[0], rgba[0], rgba[0], rgba[1]))
                        else:
                            expected.extend(rgba)
                    for interlace in [0, 1]:
                        with self.subTest(color=color, depth=depth, width=width, height=height, interlace=interlace):
                            png = image(width, height, depth, color, pixels, interlace, palette, transparency)
                            self.assertEqual(mod.decode_png(png), (width, height, bytes(expected)))

    def test_16_bit_transparency_matches_before_precision_reduction(self):
        for color, pixels, key, expected in [
            (0, [(0x1234,), (0x1235,)], (0x1234,), bytes((18, 18, 18, 0, 18, 18, 18, 255))),
            (2, [(0x1234, 0x5678, 0x9abc), (0x1235, 0x5678, 0x9abc)],
             (0x1234, 0x5678, 0x9abc), bytes((18, 86, 154, 0, 18, 86, 154, 255)))]:
            for interlace in [0, 1]:
                png = image(2, 1, 16, color, pixels, interlace,
                            transparency=struct.pack('>' + 'H' * len(key), *key))
                self.assertEqual(mod.decode_png(png)[2], expected)

    def test_interlace_bounds_compression_and_integrity(self):
        for raw in [b'', b'\0' * 8, b'\0' * 10, b'\5' + b'\0' * 8, b'\0' * 100000]:
            png = image(1, 1, 16, 6, [(0, 0, 0, 0)], 1, raw_override=raw)
            with self.subTest(length=len(raw)), self.assertRaises(mod.ImportError):
                mod.decode_png(png)
        png = image(1, 1, 16, 6, [(0, 0, 0, 0)], 1)
        damaged = bytearray(png)
        damaged[-5] ^= 1
        with self.assertRaisesRegex(mod.ImportError, 'checksum'):
            mod.decode_png(damaged)
        with patch.object(mod, 'MAX_PIXELS', 0), self.assertRaisesRegex(mod.ImportError, 'dimensions'):
            mod.decode_png(png)
        with self.assertRaisesRegex(mod.ImportError, 'trailing'):
            mod.decode_png(png + b'extra')
        with self.assertRaisesRegex(mod.ImportError, 'missing PNG end'):
            mod.decode_png(png[:-12])
        # Rebuild the IDAT with an extra zlib stream, preserving the PNG CRC.
        compressed = zlib.compress(b'\0' * 9) + zlib.compress(b'extra')
        extra_stream = png[:33] + mod.png_chunk(b'IDAT', compressed) + mod.png_chunk(b'IEND', b'')
        with self.assertRaisesRegex(mod.ImportError, 'size mismatch'):
            mod.decode_png(extra_stream)

    def test_invalid_color_depth_and_interlace(self):
        for depth, color, interlace in [(16, 3, 0), (4, 6, 0), (8, 1, 0), (8, 6, 2)]:
            png = (b'\x89PNG\r\n\x1a\n' + mod.png_chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, depth, color, 0, 0, interlace))
                   + mod.png_chunk(b'IEND', b''))
            with self.subTest(depth=depth, color=color, interlace=interlace), self.assertRaises(mod.ImportError):
                mod.decode_png(png)


if __name__ == '__main__':
    unittest.main()
