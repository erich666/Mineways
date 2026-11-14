#!/usr/bin/env python3
"""Check the showcase layout and the Anvil palette packing without mod downloads."""
import gzip
from pathlib import Path
import tempfile
import unittest
import zlib

from make_atm10_showcase import make_showcase
from test_preview_demo import read_nbt


class ShowcaseGeneratorTests(unittest.TestCase):
    def test_gallery_states_and_schematic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'gallery'
            catalog = make_showcase(root)
            self.assertEqual(catalog['block_count'], 24)
            self.assertEqual(catalog['expected_non_air_blocks'], 304)
            self.assertEqual(len({tuple(s['position']) for s in catalog['showcase_blocks']}), 24)
            region = (root / 'world/region/r.0.0.mca').read_bytes()
            length = int.from_bytes(region[8192:8196], 'big')
            self.assertEqual(region[8196], 2)
            chunk = read_nbt(zlib.decompress(region[8197:8196 + length]))
            states = chunk['sections'][0]['block_states']
            palette = states['palette']
            bits = max(4, (len(palette) - 1).bit_length())
            self.assertGreater(bits, 4)
            per_long = 64 // bits
            cells = [(states['data'][i // per_long] >> ((i % per_long) * bits)) & ((1 << bits) - 1)
                     for i in range(4096)]
            self.assertEqual(sum(index != 0 for index in cells), 304)
            for sample in catalog['showcase_blocks']:
                x, y, z = sample['position']
                entry = palette[cells[x + 16 * z + (y - 64) * 256]]
                state = entry['Name']
                if 'Properties' in entry:
                    state += '[' + ','.join(k + '=' + v for k, v in sorted(entry['Properties'].items())) + ']'
                self.assertEqual(state, sample['state'])
                self.assertNotEqual(cells[x + 16 * z + 256], 0)
            schematic = read_nbt(gzip.decompress((root / 'showcase.schem').read_bytes()))
            self.assertEqual(schematic['BlockData'], bytes(cells[:1024]))
            self.assertEqual((schematic['Width'], schematic['Height'], schematic['Length']), (16, 4, 16))
            level = read_nbt(gzip.decompress((root / 'world/level.dat').read_bytes()))['Data']
            self.assertEqual((level['SpawnX'], level['SpawnY'], level['SpawnZ']), (8, 67, 8))

    def test_deterministic_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = (Path(temporary) / name for name in ('first', 'second'))
            make_showcase(first)
            make_showcase(second)
            for path in first.rglob('*'):
                if path.is_file():
                    self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())
            with self.assertRaises(FileExistsError):
                make_showcase(first)


if __name__ == '__main__':
    unittest.main()
