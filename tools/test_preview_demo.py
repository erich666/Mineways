#!/usr/bin/env python3
"""Validate the original preview's deterministic Anvil and schematic data."""
import gzip
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from make_preview_demo import make_demo


def read_nbt(data):
    stream = io.BytesIO(data)

    def unpack(fmt):
        return struct.unpack(fmt, stream.read(struct.calcsize(fmt)))[0]

    def string():
        return stream.read(unpack('>H')).decode('utf-8')

    def payload(kind):
        if kind in (1, 2, 3, 4, 5, 6):
            return unpack({1: '>b', 2: '>h', 3: '>i', 4: '>q', 5: '>f', 6: '>d'}[kind])
        if kind == 7:
            return stream.read(unpack('>i'))
        if kind == 8:
            return string()
        if kind == 9:
            entry_kind, count = unpack('>B'), unpack('>i')
            return [payload(entry_kind) for _ in range(count)]
        if kind == 10:
            result = {}
            while (child := unpack('>B')):
                name = string()
                result[name] = payload(child)
            return result
        if kind == 11:
            return [unpack('>i') for _ in range(unpack('>i'))]
        if kind == 12:
            return [unpack('>Q') for _ in range(unpack('>i'))]
        raise AssertionError('Unexpected NBT type: ' + str(kind))

    kind = unpack('>B')
    string()
    result = payload(kind)
    assert not stream.read(), 'Trailing NBT bytes'
    return result


class PreviewDemoTests(unittest.TestCase):
    def test_scalar_and_array_types(self):
        from make_preview_demo import compound, tag
        data = compound('', tag(4, 'long', struct.pack('>q', -7))
                        + tag(5, 'float', struct.pack('>f', 1.5))
                        + tag(6, 'double', struct.pack('>d', 2.5))
                        + tag(11, 'offset', struct.pack('>4i', 3, -1, 0, 2))
                        + tag(12, 'packed', struct.pack('>iQ', 1, 0x8000000000000000)))
        self.assertEqual(read_nbt(data), {'long': -7, 'float': 1.5, 'double': 2.5,
                                          'offset': [-1, 0, 2], 'packed': [0x8000000000000000]})

    def test_world_and_schematic_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'demo'
            expected = make_demo(root)
            region = (root / 'world/region/r.0.0.mca').read_bytes()
            sector, count = int.from_bytes(region[:3], 'big'), region[3]
            self.assertEqual(sector, 2)
            self.assertEqual(len(region), 8192 + count * 4096)
            length = int.from_bytes(region[8192:8196], 'big')
            self.assertEqual(region[8196], 2)
            chunk = read_nbt(zlib.decompress(region[8197:8196 + length]))
            section = chunk['sections'][0]
            self.assertEqual(section['Y'], 4)
            states = section['block_states']
            cells = [(value >> shift) & 15 for value in states['data'] for shift in range(0, 64, 4)]
            self.assertEqual(sum(value != 0 for value in cells), expected['non_air_blocks'])
            for sample in expected['sample_blocks']:
                x, y, z = sample['position']
                entry = states['palette'][cells[x + z * 16 + (y - 64) * 256]]
                name = entry['Name']
                if 'Properties' in entry:
                    name += '[' + ','.join(k + '=' + v for k, v in sorted(entry['Properties'].items())) + ']'
                self.assertEqual(name, sample['state'])
            schematic = read_nbt(gzip.decompress((root / 'demo.schem').read_bytes()))
            self.assertEqual(schematic['BlockData'], bytes(cells[:1024]))
            self.assertEqual((schematic['Width'], schematic['Height'], schematic['Length']), (16, 4, 16))
            self.assertEqual(set(schematic['Palette']), {'minecraft:air', *expected['schematic_states']})
            custom = root / 'world/dimensions/mineways_demo/gallery/region/r.0.0.mca'
            self.assertEqual(custom.read_bytes(), region)
            level = read_nbt(gzip.decompress((root / 'world/level.dat').read_bytes()))
            self.assertEqual(level['Data']['SpawnY'], 66)

    def test_deterministic_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = (Path(temporary) / name for name in ('first', 'second'))
            make_demo(first)
            make_demo(second)
            for path in first.rglob('*'):
                if path.is_file():
                    self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())
            with self.assertRaises(FileExistsError):
                make_demo(first)
            expected = json.loads((first / 'expected.json').read_text())
            self.assertEqual(len(expected['sample_blocks']), 8)


if __name__ == '__main__':
    unittest.main()
