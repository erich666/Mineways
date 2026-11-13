#!/usr/bin/env python3
"""Generate original preview assets and a tiny Anvil world, using only Python's standard library.

The resulting demo needs no Python or Minecraft installation. All generated art and
models are original and dedicated to the public domain under CC0-1.0.
"""
import argparse
import gzip
import json
from pathlib import Path
import struct
import zlib

NAMESPACE = 'mineways_demo'
SIDES = ('down', 'up', 'north', 'south', 'west', 'east')
COLORS = ((245, 184, 50, 255), (35, 180, 168, 255),
          (170, 90, 220, 112), (35, 75, 155, 255), (190, 95, 45, 255))


def png(pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * 64:(y + 1) * 64] for y in range(16))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 16, 16, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def text(value):
    value = value.encode('utf-8')
    return struct.pack('>H', len(value)) + value


def tag(kind, name, payload):
    return bytes((kind,)) + text(name) + payload


def integer(name, value):
    return tag(3, name, struct.pack('>i', value))


def compound(name, payload):
    return tag(10, name, payload + b'\0')


def list_tag(name, kind, entries):
    return tag(9, name, bytes((kind,)) + struct.pack('>i', len(entries)) + b''.join(entries))


def state_entry(state):
    name, _, properties = state.partition('[')
    body = tag(8, 'Name', text(name))
    if properties:
        body += compound('Properties', b''.join(tag(8, key, text(value))
                         for key, value in (pair.split('=') for pair in properties[:-1].split(','))))
    return body + b'\0'


def write_region(path, palette, cells):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Modern block states pack 16 four-bit palette indices into each long.
    longs = [sum(cells[base + offset] << (offset * 4) for offset in range(16))
             for base in range(0, 4096, 16)]
    states = list_tag('palette', 10, [state_entry(state) for state in palette])
    states += tag(12, 'data', struct.pack('>i', len(longs)) + b''.join(struct.pack('>Q', v) for v in longs))
    section = tag(1, 'Y', b'\x04') + compound('block_states', states)
    section += compound('biomes', list_tag('palette', 8, [text('minecraft:plains')])) + b'\0'
    chunk = compound('', integer('DataVersion', 3953) + integer('xPos', 0) + integer('zPos', 0)
                     + tag(8, 'Status', text('minecraft:full')) + list_tag('sections', 10, [section]))
    compressed = zlib.compress(chunk)
    payload = struct.pack('>I', len(compressed) + 1) + b'\x02' + compressed
    sectors = (len(payload) + 4095) // 4096
    header = bytearray(8192)
    header[:4] = bytes((0, 0, 2, sectors))
    path.write_bytes(header + payload + bytes(sectors * 4096 - len(payload)))


def write_schematic(path, palette, cells):
    body = integer('Version', 2) + integer('DataVersion', 3953)
    for name, value in (('Width', 16), ('Height', 4), ('Length', 16)):
        body += tag(2, name, struct.pack('>h', value))
    body += integer('PaletteMax', len(palette))
    body += compound('Palette', b''.join(integer(state, index) for index, state in enumerate(palette)))
    data = bytes(cells[:1024])
    body += tag(7, 'BlockData', struct.pack('>i', len(data)) + data)
    path.write_bytes(gzip.compress(compound('Schematic', body), mtime=0))


def make_demo(output_dir):
    """Create a fresh demo directory and return its expected.json metadata."""
    root = Path(output_dir)
    root.mkdir(parents=True, exist_ok=False)
    (root / 'textures').mkdir()
    source = root / 'source-assets'

    def put(path, value):
        path = source / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else (json.dumps(value, indent=2) + '\n').encode())

    lines = ['MINEWAYS_MOD_PACK\t2', 'dimension\tmineways_demo:gallery\t-64\t384\t1']
    texture_names = ('sunstone', 'tide', 'glass', 'arrow', 'copper')
    for index, (name, color) in enumerate(zip(texture_names, COLORS)):
        pixels = bytearray()
        for y in range(16):
            for x in range(16):
                # A white right-pointing arrow makes the sign's rotation visible.
                arrow = name == 'arrow' and ((3 <= x <= 10 and 7 <= y <= 8) or
                                              (8 <= x <= 12 and abs(y - 7.5) <= 12 - x))
                pixels.extend((250, 250, 240, 255) if arrow else color)
        data = png(pixels)
        (root / 'textures' / (name + '.png')).write_bytes(data)
        put(f'assets/{NAMESPACE}/textures/block/{name}.png', data)
        lines.append(f'texture\t{index}\t{NAMESPACE}:block/{name}\ttextures/{name}.png')

    def geometry(name, boxes, texture):
        geometry_id = len(models)
        elements = []
        lines.append(f'geometry\t{geometry_id}')
        for start, end in boxes:
            lines.append('element\t' + '\t'.join(map(str, (*start, *end))) + '\tnone\t0\t8\t8\t8\t0')
            for side in SIDES:
                lines.append(f'face\t{side}\t0\t0\t16\t16\t0\t{texture}\t-')
            elements.append({'from': start, 'to': end,
                             'faces': {side: {'uv': [0, 0, 16, 16], 'texture': '#all'} for side in SIDES}})
        lines.append('endgeometry')
        put(f'assets/{NAMESPACE}/models/block/{name}.json',
            {'textures': {'all': f'{NAMESPACE}:block/{texture_names[texture]}'}, 'elements': elements})
        models[name] = geometry_id

    models = {}
    geometry('sunstone', [([0, 0, 0], [16, 16, 16])], 0)
    geometry('tide', [([0, 0, 0], [16, 16, 16])], 1)
    geometry('glass', [([0, 0, 0], [16, 16, 16])], 2)
    geometry('arrow_sign', [([2, 6, 7], [14, 16, 9]), ([7, 0, 7], [9, 6, 9])], 3)
    geometry('sensor_base', [([3, 0, 3], [13, 4, 13])], 4)
    geometry('sensor_arm', [([7, 4, 7], [9, 14, 9]), ([7, 12, 2], [9, 14, 7])], 4)

    def state(name, refs, color):
        lines.append(f'state\t{NAMESPACE}:{name}\t{color}\t255')
        for model, turn in refs:
            lines.append(f'usemodel\t{models[model]}\t0\t{turn}\t0')
        lines.append('endstate')

    for name, color in (('sunstone', 'f5b832'), ('tide', '23b4a8'), ('glass', 'aa5adc')):
        state(name, [(name, 0)], color)
        put(f'assets/{NAMESPACE}/blockstates/{name}.json', {'variants': {'': {'model': f'{NAMESPACE}:block/{name}'}}})
    variants = {}
    for facing, turn in (('north', 0), ('east', 90)):
        state(f'arrow_sign[facing={facing}]', [('arrow_sign', turn)], '234b9b')
        variants[f'facing={facing}'] = {'model': f'{NAMESPACE}:block/arrow_sign', 'y': turn}
        for connected in ('false', 'true'):
            refs = [('sensor_base', 0)] + ([('sensor_arm', turn)] if connected == 'true' else [])
            state(f'sensor[connected={connected},facing={facing}]', refs, 'be5f2d')
    put(f'assets/{NAMESPACE}/blockstates/arrow_sign.json', {'variants': variants})
    put(f'assets/{NAMESPACE}/blockstates/sensor.json', {'multipart': [
        {'apply': {'model': f'{NAMESPACE}:block/sensor_base'}},
        *[{'when': {'connected': 'true', 'facing': facing},
           'apply': {'model': f'{NAMESPACE}:block/sensor_arm', 'y': turn}}
          for facing, turn in (('north', 0), ('east', 90))]]})
    put(f'data/{NAMESPACE}/dimension/gallery.json', {'type': {'min_y': -64, 'height': 384, 'coordinate_scale': 1}})
    (root / 'manifest.mwpack').write_text('\n'.join(lines) + '\n', encoding='utf-8')

    samples = [((3, 65, 3), 'sunstone'), ((6, 65, 3), 'tide'), ((9, 65, 3), 'glass'),
               ((3, 65, 7), 'arrow_sign[facing=north]'), ((7, 65, 7), 'arrow_sign[facing=east]'),
               ((3, 65, 11), 'sensor[connected=false,facing=north]'),
               ((7, 65, 11), 'sensor[connected=true,facing=north]'),
               ((11, 65, 11), 'sensor[connected=true,facing=east]')]
    palette = ['minecraft:air', 'minecraft:stone'] + [f'{NAMESPACE}:{name}' for _, name in samples]
    cells = [0] * 4096
    cells[:256] = [1] * 256
    for index, ((x, y, z), _) in enumerate(samples, 2):
        cells[x + z * 16 + (y - 64) * 256] = index
    world = root / 'world'
    write_region(world / 'region/r.0.0.mca', palette, cells)
    write_region(world / f'dimensions/{NAMESPACE}/gallery/region/r.0.0.mca', palette, cells)
    level = integer('version', 19133) + integer('DataVersion', 3953)
    level += compound('Version', integer('Id', 3953) + tag(8, 'Name', text('1.21.1')))
    level += tag(8, 'LevelName', text('Mineways Original Preview Gallery'))
    level += integer('SpawnX', 8) + integer('SpawnY', 66) + integer('SpawnZ', 8)
    (world / 'level.dat').write_bytes(gzip.compress(compound('', compound('Data', level)), mtime=0))
    write_schematic(root / 'demo.schem', palette, cells)
    expected = {'selection_min': [0, 64, 0], 'selection_max': [15, 67, 15],
                'schematic_selection_min': [0, 0, 0], 'schematic_selection_max': [15, 3, 15],
                'dimension': f'{NAMESPACE}:gallery', 'schematic_states': palette[1:],
                'expected_texture_colors': [list(color) for color in COLORS],
                'sample_blocks': [{'position': list(pos), 'state': f'{NAMESPACE}:{name}'} for pos, name in samples],
                'non_air_blocks': 264, 'minimum_faces': 48}
    (root / 'expected.json').write_text(json.dumps(expected, indent=2) + '\n', encoding='utf-8')
    (root / 'LICENSE.txt').write_text('Original demo models, textures, and world: CC0-1.0.\n'
        'Dedicated to the public domain. No Minecraft or modpack assets are included.\n'
        'https://creativecommons.org/publicdomain/zero/1.0/\n', encoding='utf-8')
    # Script paths use the process working directory on both platforms.
    (root / 'sample.mwscript').write_text('\n'.join([
        'Mod pack: ./manifest.mwpack', 'Minecraft world: ./world',
        'Set render type: Wavefront OBJ absolute indices',
        'File type: Export individual textures to directory demo-textures',
        'Export separate types: YES', 'Individual blocks: YES', 'Export lesser blocks: YES',
        'Selection location min to max: 0, 64, 0 to 15, 67, 15',
        'Export for Rendering: demo.obj', 'Close', '']), encoding='utf-8')
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', required=True, help='new directory for the generated demo')
    args = parser.parse_args()
    make_demo(args.output_dir)
    print('Created demo: ' + str(Path(args.output_dir).resolve()))


if __name__ == '__main__':
    main()
