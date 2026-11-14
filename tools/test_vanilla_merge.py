#!/usr/bin/env python3
"""Compare vanilla exports with fork master, including a mod-load/clear cycle.

The original Anvil fixture is a small vanilla house and garden, not a Debug World
or a player's save. Windows uses the original executables' -headless command.
macOS master uses --baseline-exports from the unmodified released GUI.
--prepare-only writes its fixture and standard Import Settings script.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import make_preview_demo as nbt
from test_mod_windows import read_textured_faces
from test_preview_package import app_command, app_environment
from test_preview_demo import read_nbt

ROOT = Path(__file__).resolve().parents[1]
BOUNDS = '0, 64, 0 to 15, 71, 15'
STATES = ['minecraft:air', 'minecraft:stone', 'minecraft:dirt', 'minecraft:grass_block[snowy=false]',
          'minecraft:oak_planks', 'minecraft:oak_log[axis=y]', 'minecraft:glass',
          'minecraft:oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]',
          'minecraft:stone_brick_slab[type=bottom,waterlogged=false]', 'minecraft:torch',
          'minecraft:rail[shape=east_west,waterlogged=false]',
          'minecraft:oak_fence[east=false,north=false,south=false,waterlogged=false,west=false]',
          'minecraft:oak_button[face=wall,facing=north,powered=false]',
          'minecraft:water[level=0]', 'minecraft:chest[facing=north,type=single,waterlogged=false]']


def make_world(world):
    world.mkdir()
    cells = [0] * 4096
    def put(x, y, z, state):
        cells[x + z * 16 + (y - 64) * 256] = state
    for x in range(16):
        for z in range(16):
            put(x, 64, z, 1)
            if 1 <= x <= 6 and 1 <= z <= 6:
                put(x, 65, z, 2)
                put(x, 66, z, 3)
    for x in range(8, 14):
        for z in range(8, 14):
            put(x, 69, z, 4)
            if x in (8, 13) or z in (8, 13):
                for y in range(65, 69):
                    put(x, y, z, 5 if x in (8, 13) and z in (8, 13) else 4)
                if x in (10, 11) or z in (10, 11):
                    put(x, 66, z, 6)
    for y in (65, 66):
        put(10, y, 8, 0)
    for x, z, state in ((7, 10, 7), (6, 10, 8), (11, 11, 9), (6, 12, 10),
                        (2, 10, 11), (3, 12, 13), (10, 10, 14)):
        put(x, 65, z, state)
    put(9, 66, 7, 12)
    for x, z in ((2, 12), (4, 12), (3, 11), (3, 13)):
        put(x, 65, z, 1)
    nbt.write_region(world / 'region/r.0.0.mca', STATES, cells)
    data = nbt.integer('version', 19133) + nbt.integer('DataVersion', 3953)
    data += nbt.compound('Version', nbt.integer('Id', 3953) + nbt.tag(8, 'Name', nbt.text('1.21.1')))
    data += nbt.tag(8, 'LevelName', nbt.text('Original vanilla house and garden'))
    data += nbt.integer('SpawnX', 4) + nbt.integer('SpawnY', 68) + nbt.integer('SpawnZ', 4)
    (world / 'level.dat').write_bytes(gzip.compress(nbt.compound('', nbt.compound('Data', data)), mtime=0))
    return {(i % 16, i // 256, i // 16 % 16): STATES[value]
            for i, value in enumerate(cells[:16 * 16 * 8]) if value}


def schematic_states(path):
    raw = path.read_bytes()
    tree = read_nbt(gzip.decompress(raw) if raw.startswith(b'\x1f\x8b') else raw)
    tree = tree.get('Schematic', tree)
    blocks = tree.get('Blocks', tree)
    palette = {index: state for state, index in blocks['Palette'].items()}
    data = blocks.get('Data', blocks.get('BlockData'))
    indices, value, shift = [], 0, 0
    for byte in data:
        value |= (byte & 127) << shift
        if byte & 128:
            shift += 7
        else:
            indices.append(value)
            value, shift = 0, 0
    width, height, length = (tree[key] for key in ('Width', 'Height', 'Length'))
    if shift or len(indices) != width * height * length:
        raise AssertionError('Invalid schematic block array')
    return {(i % width, i // (width * length), i // width % length): palette[index]
            for i, index in enumerate(indices) if palette[index] != 'minecraft:air'}


def obj_signatures(path):
    geometry, pixels = [], []
    for corners, (width, height, rgba) in read_textured_faces(path):
        positions = tuple(tuple(round(value, 5) for value in point) for point, uv in corners)
        if not all(math.isfinite(value) for point in positions for value in point):
            raise AssertionError('Non-finite geometry')
        geometry.append(positions)
        if len(corners) not in (3, 4):
            raise AssertionError('Expected triangle or quad faces')
        samples = bytearray()
        uv = [corner[1] for corner in corners]
        for y in range(16):
            t = (y + .5) / 16
            for x in range(16):
                s = (x + .5) / 16
                if len(corners) == 3 and s + t >= 1:
                    continue
                weights = (1-s-t, s, t) if len(corners) == 3 else ((1-s)*(1-t), s*(1-t), s*t, (1-s)*t)
                u, v = (sum(weight * point[axis] for weight, point in zip(weights, uv)) for axis in range(2))
                px = min(width - 1, max(0, int(u * width)))
                py = min(height - 1, max(0, int((1-v) * height)))
                offset = (py * width + px) * 4
                samples.extend(rgba[offset:offset + 4])
        pixels.append((positions, hashlib.sha256(samples).hexdigest(), len(samples) // 4))
    if not geometry:
        raise AssertionError('Empty vanilla export')
    return Counter(geometry), Counter(pixels)


def export_commands(world, folder):
    return ['Minecraft world: ' + str(world), 'Set render type: Wavefront OBJ absolute indices',
            'Center model: NO', 'Export lesser blocks: YES', 'Individual blocks: YES',
            'Simplify mesh: NO', 'Double all billboard faces: NO',
            'Create composite overlay faces: NO', 'Create block faces at the borders: YES',
            'Use biomes: NO', 'Rotate model 0 degrees',
            'Make Z the up direction instead of Y: NO',
            'Scale model by making each block 1000 mm high',
            'Units for the model vertex data itself: meters',
            'Texture output RGB: YES', 'Texture output A: YES', 'Texture output RGBA: YES',
            'Selection location min to max: ' + BOUNDS,
            'File type: Export individual textures to directory individual-textures',
            'Export for Rendering: ' + str(folder / 'individual.obj'),
            'File type: Export full color texture patterns',
            'Export for Rendering: ' + str(folder / 'mosaic.obj'),
            'Export Schematic: ' + str(folder / 'world.schem')]


def run_script(executable, commands, folder):
    folder.mkdir()
    script = folder / 'parity.mwscript'
    logging = (['Show informational: script', 'Show warning: script', 'Show error: script',
                'Save log file: ' + str(folder / 'engine.log')] if os.name == 'nt' else [])
    script.write_text('\n'.join([*logging, *commands, 'Close', '']), encoding='utf-8')
    run = subprocess.run(app_command(executable, script), cwd=folder, env=app_environment(),
                         stdin=subprocess.DEVNULL, capture_output=True, text=True,
                         errors='replace', timeout=180)
    (folder / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    if run.returncode:
        raise AssertionError(f'Vanilla export failed ({run.returncode}): {folder}\n{run.stdout}\n{run.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-executable', type=Path)
    parser.add_argument('--baseline-exports', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--candidate-executable', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[__file__])
        return
    if not args.prepare_only and (args.baseline_executable is None) == (args.baseline_exports is None):
        parser.error('Choose --baseline-executable or --baseline-exports')
    if args.output is None or (args.candidate_executable is None and not args.prepare_only):
        parser.error('--output and --candidate-executable are required')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    baseline = args.baseline_executable.resolve() if args.baseline_executable else None
    world = output / 'ordinary vanilla world'
    expected = make_world(world)
    before = {p.relative_to(world).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in world.rglob('*') if p.is_file()}
    mod = output / 'original-mod-fixture'
    nbt.make_demo(mod)
    fresh, after, control = [output / name for name in ('candidate-fresh', 'candidate-after-mods', 'master')]
    if args.prepare_only:
        control.mkdir()
        script = output / 'master-import.mwscript'
        script.write_text('\n'.join(export_commands(world, control)) + '\n', encoding='utf-8')
        print('Import into the unmodified master GUI: ' + str(script), flush=True)
        return
    if args.baseline_exports:
        control = args.baseline_exports.resolve()
    else:
        run_script(baseline, export_commands(world, control), control)
    run_script(args.candidate_executable.resolve(), export_commands(world, fresh), fresh)
    cycle_before = output / 'candidate-cycle-before'
    cycle_before.mkdir()
    commands = [*export_commands(world, cycle_before), 'Mod pack: ' + str(mod / 'manifest.mwpack'), 'Minecraft world: ' + str(mod / 'world'),
                'Selection location min to max: 0, 64, 0 to 15, 67, 15',
                'Set render type: Wavefront OBJ absolute indices', 'Export lesser blocks: YES',
                'File type: Export individual textures to directory mod-textures',
                'Export for Rendering: ' + str(after / 'mod-active.obj'), 'Clear mod pack']
    run_script(args.candidate_executable.resolve(), [*commands, *export_commands(world, after)], after)
    comparison = {}
    for mode in ('individual', 'mosaic'):
        original = obj_signatures(control / (mode + '.obj'))
        for name, folder in (('fresh', fresh), ('same_process_before', cycle_before), ('after_mod_clear', after)):
            actual = obj_signatures(folder / (mode + '.obj'))
            if original != actual:
                raise AssertionError(f'Vanilla {mode} geometry or sampled texture pixels differ: {name}')
        comparison[mode] = {'faces': sum(original[0].values()),
                            'face_texture_samples': sum(key[2] * count for key, count in original[1].items()),
                            'geometry_sha256': hashlib.sha256(json.dumps(sorted(original[0].items())).encode()).hexdigest(),
                            'sampled_pixels_sha256': hashlib.sha256(json.dumps(sorted(original[1].items())).encode()).hexdigest()}
    baseline_states = schematic_states(control / 'world.schem')
    if 'mw_mod_' not in (after / 'mod-active.mtl').read_text(errors='replace'):
        raise AssertionError('Transition did not export actual imported mod materials')
    for folder in (fresh, cycle_before, after):
        if schematic_states(folder / 'world.schem') != baseline_states:
            raise AssertionError('Vanilla Sponge names/properties differ by voxel: ' + folder.name)
    if {position: value.split('[', 1)[0] for position, value in baseline_states.items()} != {
            position: value.split('[', 1)[0] for position, value in expected.items()}:
        raise AssertionError('Vanilla Sponge lost expected block names or positions')
    if before != {p.relative_to(world).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in world.rglob('*') if p.is_file()}:
        raise AssertionError('Vanilla save changed')
    result = {'baseline': str(baseline or control), 'candidate': str(args.candidate_executable.resolve()),
              'bounds': BOUNDS, 'fixture': 'original ordinary vanilla house/garden, Minecraft 1.21.1',
              'geometry_quantization_blocks': 1e-5,
              'vanilla_states': len(set(expected.values())), 'non_air_voxels': len(expected),
              'sponge_names_and_properties_by_position': True,
              'sponge_states_sha256': hashlib.sha256(json.dumps(sorted(baseline_states.items())).encode()).hexdigest(),
              'candidate_executable_sha256': hashlib.sha256(args.candidate_executable.read_bytes()).hexdigest(),
              'same_process_before_and_after_mod_clear': True, 'mod_materials_exercised': True,
              'save_unchanged': True,
              'fresh_and_after_mod_clear_match_master': True, 'exports': comparison}
    (output / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: vanilla parity: ' + json.dumps(result), flush=True)


class Checks(unittest.TestCase):
    def test_original_world_uses_all_states(self):
        with tempfile.TemporaryDirectory() as temporary:
            states = make_world(Path(temporary) / 'world')
            self.assertEqual(set(states.values()), set(STATES) - {'minecraft:air'})
            self.assertGreater(len(states), 300)
    def test_pixels_and_triangle_faces(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            obj = root / 'triangle.obj'
            obj.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\n'
                           'vt 0 0\nvt 1 0\nvt 0 1\nusemtl test\nf 1/1 2/2 3/3\n')
            obj.with_suffix('.mtl').write_text('newmtl test\nmap_Kd texture.png\n')
            texture = root / 'texture.png'
            texture.write_bytes(nbt.png(bytes((255, 0, 0, 255)) * 256))
            first = obj_signatures(obj)
            texture.write_bytes(nbt.png(bytes((0, 255, 0, 255)) * 256))
            second = obj_signatures(obj)
            self.assertEqual(first[0], second[0])
            self.assertNotEqual(first[1], second[1])
            self.assertEqual(sum(first[0].values()), 1)



if __name__ == '__main__':
    main()
