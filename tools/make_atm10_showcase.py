#!/usr/bin/env python3
"""Create an original Mineways gallery fixture for the pinned ATM10 8.3 assets.

No mod assets or private world data are included. This is an export fixture,
not a Minecraft gameplay save. Generation uses only the Python standard library.
"""
import argparse
import gzip
import json
from pathlib import Path
import struct
import zlib

from make_preview_demo import compound, integer, list_tag, state_entry, tag, text, write_schematic


# Each group occupies one row. All models work without saved block-entity data.
GROUPS = (
    ('Materials', 'minecraft:light_blue_concrete', (
        ('Fluix block', 'ae2:fluix_block'),
        ('Quartz block', 'ae2:quartz_block'),
        ('Osmium block', 'mekanism:block_osmium'),
        ('Steel block', 'mekanism:block_steel'),
        ('Fluorite block', 'mekanism:block_fluorite'),
        ('Canopy planks', 'twilightforest:canopy_planks'),
    )),
    ('Machines', 'minecraft:orange_concrete', (
        ('Charger', 'ae2:charger[facing=north,spin=0]'),
        ('Inscriber', 'ae2:inscriber[facing=north,spin=0]'),
        ('Online controller', 'ae2:controller[state=online,type=block]'),
        ('Full energy cell', 'ae2:energy_cell[fullness=4]'),
        ('Advanced energy cube', 'mekanism:advanced_energy_cube[facing=north]'),
        ('LV capacitor', 'immersiveengineering:capacitor_lv'),
    )),
    ('Glass and shapes', 'minecraft:lime_concrete', (
        ('Quartz glass', 'ae2:quartz_glass'),
        ('Spatial pylon', 'ae2:spatial_pylon'),
        ('Firefly jar', 'twilightforest:firefly_jar'),
        ('Canopy stairs', 'twilightforest:canopy_stairs[facing=north,half=bottom,shape=straight]'),
        ('Canopy fence', 'twilightforest:canopy_fence[east=false,north=false,south=false,west=false]'),
        ('Standing lantern', 'immersiveengineering:lantern[facing=up]'),
    )),
    ('Workshop', 'minecraft:purple_concrete', (
        ('Storage chest', 'sophisticatedstorage:chest[facing=north,type=single]'),
        ('Lit stove', 'farmersdelight:stove[facing=north,lit=true]'),
        ('Canvas sign', 'farmersdelight:canvas_sign[rotation=8]'),
        ('XYCraft buffer', 'xycraft_machines:buffer[direction=north,forward=0]'),
        ('Filtered item pipe', 'xycraft_machines:pipe_item[filtered=true]'),
        ('Ultimate cable', 'mekanism:ultimate_universal_cable'),
    )),
)


def make_showcase(output_dir):
    """Write a fresh gallery directory and return its catalog.json metadata."""
    root = Path(output_dir)
    root.mkdir(parents=True, exist_ok=False)
    samples = []
    columns, rows = (2, 4, 6, 9, 11, 13), (2, 6, 9, 13)
    for row, (group, pedestal, entries) in enumerate(GROUPS):
        for column, (label, state) in enumerate(entries):
            samples.append({'index': len(samples) + 1, 'label': label, 'group': group,
                            'state': state, 'position': [columns[column], 66, rows[row]]})
    palette = ['minecraft:air', 'minecraft:smooth_stone', 'minecraft:polished_deepslate',
               'minecraft:sea_lantern', 'minecraft:quartz_block']
    palette += [pedestal for _, pedestal, _ in GROUPS]
    palette += [sample['state'] for sample in samples]
    lookup = {state: index for index, state in enumerate(palette)}
    cells = [0] * 4096
    for z in range(16):
        for x in range(16):
            border = x in (0, 15) or z in (0, 15)
            state = 'minecraft:polished_deepslate' if border else 'minecraft:smooth_stone'
            if x in (7, 8) or z in (4, 7, 11):
                state = 'minecraft:quartz_block'
            if x in (0, 15) and z in (0, 15):
                state = 'minecraft:sea_lantern'
            cells[x + 16 * z] = lookup[state]
    for index, sample in enumerate(samples):
        x, _, z = sample['position']
        cells[x + 16 * z + 256] = lookup[GROUPS[index // 6][1]]
        cells[x + 16 * z + 512] = lookup[sample['state']]

    # Modern Anvil palettes pad each long independently. More than 16 states need wider entries.
    bits = max(4, (len(palette) - 1).bit_length())
    per_long = 64 // bits
    longs = [sum(value << (offset * bits) for offset, value in enumerate(cells[start:start + per_long]))
             for start in range(0, len(cells), per_long)]
    states = list_tag('palette', 10, [state_entry(state) for state in palette])
    states += tag(12, 'data', struct.pack('>i', len(longs)) + b''.join(struct.pack('>Q', value) for value in longs))
    section = tag(1, 'Y', b'\x04') + compound('block_states', states)
    section += compound('biomes', list_tag('palette', 8, [text('minecraft:plains')])) + b'\0'
    chunk = compound('', integer('DataVersion', 3955) + integer('xPos', 0) + integer('zPos', 0)
                     + tag(8, 'Status', text('minecraft:full')) + list_tag('sections', 10, [section]))
    compressed = zlib.compress(chunk)
    payload = struct.pack('>I', len(compressed) + 1) + b'\x02' + compressed
    sectors = (len(payload) + 4095) // 4096
    header = bytearray(8192)
    header[:4] = bytes((0, 0, 2, sectors))
    world = root / 'world'
    (world / 'region').mkdir(parents=True)
    (world / 'region/r.0.0.mca').write_bytes(header + payload + bytes(sectors * 4096 - len(payload)))
    level = integer('version', 19133) + integer('DataVersion', 3955)
    level += compound('Version', integer('Id', 3955) + tag(8, 'Name', text('1.21.1')))
    level += tag(8, 'LevelName', text('Mineways ATM10 Showcase'))
    level += integer('SpawnX', 8) + integer('SpawnY', 67) + integer('SpawnZ', 8)
    (world / 'level.dat').write_bytes(gzip.compress(compound('', compound('Data', level)), mtime=0))
    write_schematic(root / 'showcase.schem', palette, cells)
    catalog = {'schema_version': 1, 'name': 'Mineways ATM10 Showcase',
               'asset_snapshot': 'All the Mods 10, version 8.3, Minecraft 1.21.1',
               'world': 'world', 'schematic': 'showcase.schem',
               'selection_min': [0, 64, 0], 'selection_max': [15, 67, 15],
               'schematic_selection_min': [0, 0, 0], 'schematic_selection_max': [15, 3, 15],
               'focus': [8, 66, 8], 'block_count': len(samples),
               'expected_non_air_blocks': sum(value != 0 for value in cells),
               'showcase_blocks': samples,
               'note': 'Mineways export fixture. Not a Minecraft gameplay save. Mod assets are not included.'}
    (root / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True, help='new directory for the generated gallery')
    args = parser.parse_args()
    make_showcase(args.output_dir)
    print('Created ATM10 showcase: ' + str(args.output_dir.resolve()))


if __name__ == '__main__':
    main()
