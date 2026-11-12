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


def schematic(path):
    names = ['fixture:cube[custom=retained]', 'fixture:wedge[facing=east]', 'fixture:panel']
    body = number('Version', 2) + number('DataVersion', 3953)
    for name, value in [('Width', 3), ('Height', 1), ('Length', 1)]:
        body += tag(2, name, struct.pack('>h', value))
    body += number('PaletteMax', len(names))
    body += compound('Palette', b''.join(number(name, index) for index, name in enumerate(names)))
    body += tag(7, 'BlockData', struct.pack('>i', len(names)) + bytes(range(len(names))))
    path.write_bytes(gzip.compress(compound('Schematic', body)))
    return names


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
    assert gzip.decompress(input_path.read_bytes()).startswith(b'\x0a')
    assert (pack / 'manifest.mwpack').read_bytes().startswith(b'MINEWAYS_MOD_PACK\t1\n')
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
                     'native mod geometry, textures, schematic state round trips, custom dimensions, and syntax checks'))


if __name__ == '__main__':
    main()
