#!/usr/bin/env python3
"""Exercise native Windows mod/dimension exports. Usage: python tools/test_mod_windows.py Mineways.exe.

--fixtures-only validates fixture generation on any host without running Mineways.
"""
import argparse
import gzip
import importlib.util
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('mod_assets_fixture', ROOT / 'Mac/tests/mod_assets.py')
FIXTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURE)


def text(value):
    encoded = value.encode('utf-8')
    return struct.pack('>H', len(encoded)) + encoded


def tag(kind, name, value):
    return bytes((kind,)) + text(name) + value


def number(name, value):
    return tag(3, name, struct.pack('>i', value))


def compound(name, body):
    return tag(10, name, body + b'\0')


def schematic(path, names=None):
    if names is None:
        names = ['fixture:cube[custom=retained]', 'fixture:wedge[facing=east]', 'fixture:panel']
    assert 1 <= len(names) <= 127
    body = number('Version', 2) + number('DataVersion', 3953)
    for name, value in [('Width', len(names)), ('Height', 1), ('Length', 1)]:
        body += tag(2, name, struct.pack('>h', value))
    body += number('PaletteMax', len(names))
    body += compound('Palette', b''.join(number(name, index) for index, name in enumerate(names)))
    body += tag(7, 'BlockData', struct.pack('>i', len(names)) + bytes(range(len(names))))
    path.write_bytes(gzip.compress(compound('Schematic', body)))
    return names



def large_catalog(root):
    """A catalog exceeds runtime slots while each selection needs only three IDs/textures."""
    pack = root / 'large-catalog'
    texture_dir = pack / 'textures'
    texture_dir.mkdir(parents=True)
    texture_count, block_count = 1200, 5000
    lines = ['MINEWAYS_MOD_PACK\t1']
    for index in range(texture_count):
        # Every texture is distinct, so a content deduplication pass cannot hide the limit.
        color = bytes((index & 255, index >> 8, 127, 255))
        (texture_dir / f'{index}.png').write_bytes(FIXTURE.mod.encode_png(2, 2, color * 4))
        lines.append(f'texture\t{index}\tcatalog:texture_{index}\ttextures/{index}.png')
    for index in range(block_count):
        texture = (index - block_count + texture_count) % texture_count
        # A transparent map sample must not remove a model whose face textures are visible.
        # All selected blocks use alpha zero so dropping them cannot pass the geometry check.
        alpha = 0 if index >= block_count - 3 else 255
        lines.extend([f'state\tcatalog:block_{index}\t808080\t{alpha}',
                      'model\t0\t0\t0',
                      'element\t0\t0\t0\t16\t16\t16\tnone\t0\t8\t8\t8\t0'])
        for side in ['down', 'up', 'north', 'south', 'west', 'east']:
            lines.append(f'face\t{side}\t0\t0\t16\t16\t0\t{texture}\t{side}')
        lines.append('endstate')
    manifest = pack / 'manifest.mwpack'
    manifest.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    names = [f'catalog:block_{index}[custom=retained]' for index in range(block_count - 3, block_count)]
    input_path = root / 'large-input.schem'
    schematic(input_path, names)
    assert sum(line.startswith('state\t') for line in lines) == block_count
    assert sum(line.startswith('texture\t') for line in lines) == texture_count
    return manifest, input_path, names


def region(path, name):
    path.mkdir(parents=True, exist_ok=True)
    palette = tag(9, 'palette', b'\x0a' + struct.pack('>i', 1) + tag(8, 'Name', text(name)) + b'\0')
    section = tag(1, 'Y', b'\0') + compound('block_states', palette) + b'\0'
    chunk = compound('', number('DataVersion', 3953) + tag(9, 'sections', b'\x0a' + struct.pack('>i', 1) + section))
    payload = zlib.compress(chunk)
    data = bytearray(8192)
    data[0:4] = b'\0\0\x02\x01'
    data += struct.pack('>I', len(payload) + 1) + b'\x02' + payload
    data += bytes((-len(data)) % 4096)
    (path / 'r.0.0.mca').write_bytes(data)


def world(path):
    region(path / 'region', 'minecraft:stone')
    region(path / 'dimensions/fixture/moon/region', 'fixture:cube')
    data = number('version', 19133) + number('DataVersion', 3953)
    data += compound('Version', number('Id', 3953))
    data += tag(8, 'LevelName', text('Mod export fixture'))
    data += number('SpawnX', 0) + number('SpawnY', 0) + number('SpawnZ', 0)
    (path / 'level.dat').write_bytes(gzip.compress(compound('', compound('Data', data))))


def run(executable, root, name, commands, expect_success=True):
    script = root / (name + '.mwscript')
    log = root / (name + '.log')
    script.write_text('\n'.join([
        'Show informational: script', 'Show warning: script', 'Show error: script',
        'Save log file: ' + str(log), *commands, 'Close', '']), encoding='utf-8')
    try:
        result = subprocess.run([str(executable), '-headless', str(script)], cwd=ROOT,
                                input='', capture_output=True, text=True, errors='replace', timeout=120)
    except subprocess.TimeoutExpired as error:
        raise AssertionError('Native export timed out, possibly a Debug assertion dialog: ' + name) from error
    details = result.stdout + result.stderr
    if log.exists():
        details += log.read_text(encoding='utf-8', errors='replace')
    if expect_success and result.returncode:
        raise AssertionError(f'{name}: Mineways exited {result.returncode}\n{details}')
    return details


def check_obj(path, details):
    if not path.exists():
        raise AssertionError(f'Missing export {path}\n{details}')
    lines = path.read_text(errors='replace').splitlines()
    assert sum(line.startswith('f ') for line in lines) >= 6, 'Export contains no mod geometry'
    vertices = [tuple(map(float, line.split()[1:4])) for line in lines if line.startswith('v ')]
    assert vertices and all(math.isfinite(v) for point in vertices for v in point)
    uv = [tuple(map(float, line.split()[1:3])) for line in lines if line.startswith('vt ')]
    assert uv and all(-1e-5 <= v <= 1.00001 for point in uv for v in point), 'Invalid texture coordinates'


def verify_schematic(path, names, details):
    if not path.exists():
        raise AssertionError(f'Missing schematic {path}\n{details}')
    data = path.read_bytes()
    if data.startswith(b'\x1f\x8b'):
        data = gzip.decompress(data)
    for name in names:
        assert name.encode() in data, 'Mod state lost in schematic export: ' + name


def exercise(executable, root):
    pack = FIXTURE.build_fixture(root)
    input_path = root / 'input.schem'
    names = schematic(input_path)
    world_path = root / 'world'
    world(world_path)
    catalog_manifest, catalog_input, catalog_names = large_catalog(root)
    assert gzip.decompress(input_path.read_bytes()).startswith(b'\x0a')
    manifest_text = (pack / 'manifest.mwpack').read_text(encoding='utf-8')
    assert manifest_text.splitlines()[0] in ('MINEWAYS_MOD_PACK\t1', 'MINEWAYS_MOD_PACK\t2')
    selectors = {line.split('\t')[1] for line in manifest_text.splitlines() if line.startswith('state\t')}
    assert {'ae2:matrix_frame', 'ae2:drive[facing=north,spin=0]', 'ae2:quartz_glass',
            'ae2:spatial_pylon', 'allthemodium:molten_allthemodium_block[level=0]',
            'allthemodium:molten_allthemodium_block[level=7]'} <= selectors, 'Adapter fixture is incomplete'
    assert '\ninvisible\n' in manifest_text, 'Invisible fixture marker is missing'
    if executable is None:
        return
    common = ['Mod pack: ' + str(pack / 'manifest.mwpack'),
              'Set render type: Wavefront OBJ absolute indices',
              'Center model: NO', 'Export lesser blocks: YES', 'Individual blocks: YES']
    obj = root / 'mod.obj'
    exported = root / 'roundtrip.schem'
    details = run(executable, root, 'schematic', [*common, 'Minecraft world: ' + str(input_path),
                  'File type: Export individual textures to directory modtex',
                  'Selection location min to max: 0, 0, 0 to 2, 0, 0',
                  'Export for Rendering: ' + str(obj), 'Export Schematic: ' + str(exported)])
    check_obj(obj, details)
    verify_schematic(exported, names, details)
    assert list((root / 'modtex').glob('*.png')), 'Individual mod textures were not exported'
    second = root / 'roundtrip2.schem'
    details = run(executable, root, 'roundtrip', [*common, 'Minecraft world: ' + str(exported),
                  'Selection location min to max: 0, 0, 0 to 2, 0, 0', 'Export Schematic: ' + str(second)])
    verify_schematic(second, names, details)
    fluid_heights = []
    for index, state in enumerate(['ae2:drive[facing=north,spin=0]',
                                   'ae2:quartz_glass', 'ae2:spatial_pylon',
                                   'allthemodium:molten_allthemodium_block[level=0]',
                                   'allthemodium:molten_allthemodium_block[level=7]']):
        label = f'adapter{index}'
        source = root / (label + '.schem')
        schematic(source, [state])
        output = root / (label + '.obj')
        roundtrip = root / (label + '-roundtrip.schem')
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ' + label + 'tex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(roundtrip)])
        check_obj(output, details)
        verify_schematic(roundtrip, [state], details)
        assert list((root / (label + 'tex')).glob('*.png')), 'Adapter textures were not exported: ' + state
        if '[level=' in state:
            heights = [float(line.split()[2]) for line in output.read_text().splitlines()
                       if line.startswith('v ')]
            fluid_heights.append(max(heights) - min(heights))
    assert fluid_heights[0] > fluid_heights[1] > 0, 'Fluid levels lost their distinct surface heights'
    # Invisible models keep their state identity and leave neighboring cube faces intact.
    invisible_names = ['fixture:cube[custom=left]', 'ae2:matrix_frame[custom=retained]',
                       'fixture:cube[custom=right]']
    invisible_input = root / 'invisible-input.schem'
    schematic(invisible_input, invisible_names)
    air_input = root / 'air-input.schem'
    schematic(air_input, [invisible_names[0], 'minecraft:air', invisible_names[2]])
    geometry = []
    invisible_export = root / 'invisible-roundtrip.schem'
    for label, source in [('invisible', invisible_input), ('air', air_input)]:
        output = root / (label + '.obj')
        commands = [*common, 'Minecraft world: ' + str(source),
                    'File type: Export individual textures to directory ' + label + 'tex',
                    'Selection location min to max: 0, 0, 0 to 2, 0, 0',
                    'Export for Rendering: ' + str(output)]
        if label == 'invisible':
            commands.append('Export Schematic: ' + str(invisible_export))
        details = run(executable, root, label, commands)
        check_obj(output, details)
        geometry.append([line for line in output.read_text().splitlines()
                         if line.startswith(('v ', 'vn ', 'vt ', 'f '))])
        if label == 'invisible':
            verify_schematic(invisible_export, invisible_names, details)
    assert geometry[0] == geometry[1], 'Invisible model changed visible geometry or occluded a neighbor'
    invisible_second = root / 'invisible-roundtrip2.schem'
    details = run(executable, root, 'invisible_roundtrip', [*common,
                  'Minecraft world: ' + str(invisible_export),
                  'Selection location min to max: 0, 0, 0 to 2, 0, 0',
                  'Export Schematic: ' + str(invisible_second)])
    verify_schematic(invisible_second, invisible_names, details)
    dim_obj = root / 'dimension.obj'
    details = run(executable, root, 'dimension', [*common, 'Minecraft world: ' + str(world_path),
                  'Dimension: fixture:moon', 'File type: Export individual textures to directory dimtex',
                  'Selection location min to max: 0, 0, 0 to 0, 0, 0', 'Export for Rendering: ' + str(dim_obj)])
    check_obj(dim_obj, details)
    assert '# Dimension: fixture:moon' in dim_obj.read_text(errors='replace'), 'Dimension identity missing from export'
    # A malformed script must fail its syntax pass before clearing the active pack or exporting.
    bad_obj = root / 'invalid.obj'
    run(executable, root, 'invalid', [*common, 'Minecraft world: ' + str(world_path),
        'Dimension:', 'Export for Rendering: ' + str(bad_obj)], expect_success=False)
    assert not bad_obj.exists(), 'Invalid dimension command reached export'
    catalog_obj = root / 'catalog.obj'
    catalog_schematic = root / 'catalog.schem'
    details = run(executable, root, 'large_catalog', [*common,
        'Mod pack: ' + str(catalog_manifest), 'Minecraft world: ' + str(catalog_input),
        'File type: Export individual textures to directory catalogtex',
        'Selection location min to max: 0, 0, 0 to 2, 0, 0',
        'Export for Rendering: ' + str(catalog_obj), 'Export Schematic: ' + str(catalog_schematic)])
    check_obj(catalog_obj, details)
    verify_schematic(catalog_schematic, catalog_names, details)
    textures = list((root / 'catalogtex').glob('*.png'))
    assert textures, 'Large catalog did not export selected textures'
    observed_colors = set()
    for texture in textures:
        _, _, pixels = FIXTURE.mod.decode_png(texture.read_bytes())
        observed_colors.update(tuple(pixels[i:i + 4]) for i in range(0, len(pixels), 4))
    for index in range(1197, 1200):
        assert (index & 255, index >> 8, 127, 255) in observed_colors, 'Late catalog texture was lost'



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', nargs='?')
    parser.add_argument('--fixtures-only', action='store_true')
    args = parser.parse_args()
    if not args.fixtures_only and not args.executable:
        parser.error('provide a Mineways executable or --fixtures-only')
    executable = Path(args.executable).resolve() if not args.fixtures_only else None
    if executable and not executable.is_file():
        parser.error('Mineways executable does not exist: ' + str(executable))
    with tempfile.TemporaryDirectory(prefix='mineways mod windows ') as temporary:
        exercise(executable, Path(temporary))
    print('PASS: ' + ('portable Windows export fixtures' if executable is None else
                     'native mod geometry, adapters, invisible states, textures, schematic round trips, custom dimensions, large catalogs, and syntax checks'))


if __name__ == '__main__':
    main()
