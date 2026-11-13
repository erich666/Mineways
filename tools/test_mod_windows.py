#!/usr/bin/env python3
"""Exercise native Windows mod/dimension exports. Usage: python tools/test_mod_windows.py Mineways.exe.

--fixtures-only validates fixture generation on any host without running Mineways.
--pack manifest.mwpack additionally tests external real assets without using a live world.
--output new-directory retains generated fixtures, logs, and exports for local diagnosis.
"""
import argparse
import gzip
import importlib.util
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time
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


def schematic(path, names=None, indices=None, dimensions=None, block_entities=()):
    if names is None:
        names = ['fixture:cube[custom=retained]', 'fixture:wedge[facing=east]', 'fixture:panel']
    assert 1 <= len(names) <= 100000
    if indices is None:
        indices = list(range(len(names)))
    if dimensions is None:
        dimensions = (len(indices), 1, 1)
    assert all(1 <= value <= 32767 for value in dimensions)
    assert math.prod(dimensions) == len(indices)
    assert len(names) <= len(indices) <= 16 * 1024 * 1024
    payload = bytearray()
    for value in indices:
        assert 0 <= value < len(names)
        while value >= 128:
            payload.append((value & 127) | 128)
            value >>= 7
        payload.append(value)
    body = number('Version', 2) + number('DataVersion', 3953)
    for name, value in zip(('Width', 'Height', 'Length'), dimensions):
        body += tag(2, name, struct.pack('>h', value))
    body += number('PaletteMax', len(names))
    body += compound('Palette', b''.join(number(name, index) for index, name in enumerate(names)))
    body += tag(7, 'BlockData', struct.pack('>i', len(payload)) + payload)
    entities = []
    for entry in block_entities:
        payload = tag(8, 'Id', text(entry['id']))
        payload += tag(11, 'Pos', struct.pack('>4i', 3, *entry['pos']))
        if 'dummy' in entry:
            payload += number('dummy', entry['dummy'])
        if 'position' in entry:
            payload += tag(11, 'posInMB', struct.pack('>4i', 3, *entry['position']))
        entities.append(payload + b'\0')
    if entities:
        body += tag(9, 'BlockEntities', b'\x0a' + struct.pack('>i', len(entities)) + b''.join(entities))
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


def region(path, name, chunk_x=0, chunk_z=0):
    path.mkdir(parents=True, exist_ok=True)
    palette = tag(9, 'palette', b'\x0a' + struct.pack('>i', 1) + tag(8, 'Name', text(name)) + b'\0')
    section = tag(1, 'Y', b'\0') + compound('block_states', palette) + b'\0'
    chunk = compound('', number('DataVersion', 3953) + number('xPos', chunk_x) + number('zPos', chunk_z)
                     + tag(9, 'sections', b'\x0a' + struct.pack('>i', 1) + section))
    payload = zlib.compress(chunk)
    data = bytearray(8192)
    slot = 4 * (chunk_x % 32 + 32 * (chunk_z % 32))
    data[slot:slot+4] = b'\0\0\x02\x01'
    data += struct.pack('>I', len(payload) + 1) + b'\x02' + payload
    data += bytes((-len(data)) % 4096)
    (path / f'r.{chunk_x // 32}.{chunk_z // 32}.mca').write_bytes(data)


def world(path):
    region(path / 'region', 'minecraft:stone')
    region(path / 'dimensions/fixture/moon/region', 'fixture:cube')
    data = number('version', 19133) + number('DataVersion', 3953)
    data += compound('Version', number('Id', 3953))
    data += tag(8, 'LevelName', text('Mod export fixture'))
    data += number('SpawnX', 0) + number('SpawnY', 0) + number('SpawnZ', 0)
    (path / 'level.dat').write_bytes(gzip.compress(compound('', compound('Data', data))))


def check_script_world_order(commands):
    loaded = False
    for line_number, line in enumerate(commands, 1):
        command = line.partition(':')[0].strip().lower()
        if command == 'minecraft world':
            loaded = True
        elif command == 'selection location min to max' and not loaded:
            raise AssertionError(f'Selection precedes world loading at command {line_number}')


def write_script(root, name, commands):
    check_script_world_order(commands)
    script = root / (name + '.mwscript')
    log = root / (name + '.log')
    script.write_text('\n'.join([
        'Show informational: script', 'Show warning: script', 'Show error: script',
        'Save log file: ' + str(log), *commands, 'Close', '']), encoding='utf-8')
    return script, log


def process_cpu_summary(process):
    if os.name != 'nt':
        return 'CPU time unavailable on this platform'
    import ctypes
    from ctypes import wintypes
    get_times = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    get_times.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    get_times.restype = wintypes.BOOL
    created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
    if not get_times(wintypes.HANDLE(int(process._handle)), ctypes.byref(created),
                     ctypes.byref(exited), ctypes.byref(kernel), ctypes.byref(user)):
        return f'CPU time unavailable (GetProcessTimes error {ctypes.get_last_error()})'
    kernel_seconds = ((kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) / 10000000
    user_seconds = ((user.dwHighDateTime << 32) | user.dwLowDateTime) / 10000000
    return (f'CPU {kernel_seconds + user_seconds:.3f}s '
            f'(user {user_seconds:.3f}s, kernel {kernel_seconds:.3f}s)')


def run(executable, root, name, commands, expect_success=True, timeout=120):
    script, log = write_script(root, name, commands)
    print('Native export: ' + name, flush=True)
    started = time.monotonic()
    with subprocess.Popen([str(executable), '-headless', str(script)], cwd=ROOT,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, errors='replace') as process:
        try:
            stdout, stderr = process.communicate(input='', timeout=timeout)
        except subprocess.TimeoutExpired as error:
            elapsed = time.monotonic() - started
            # Inspect the live process before killing it. High CPU suggests active work,
            # while low CPU is consistent with waiting (including a modal assertion).
            try:
                cpu = process_cpu_summary(process)
            finally:
                process.kill()
                stdout, stderr = process.communicate()
            details = stdout + stderr
            if log.exists():
                details += log.read_text(encoding='utf-8', errors='replace')
            raise AssertionError(f'Native export timed out: {name} '
                                 f'(wall {elapsed:.3f}s, {cpu})\n{details}') from error
        print(f'Native export completed: {name} '
              f'(wall {time.monotonic() - started:.3f}s, {process_cpu_summary(process)})', flush=True)
        details = stdout + stderr
        if log.exists():
            details += log.read_text(encoding='utf-8', errors='replace')
        if expect_success and process.returncode:
            raise AssertionError(f'{name}: Mineways exited {process.returncode}\n{details}')
        return details


def check_obj(path, details, minimum_faces=6):
    if not path.exists():
        raise AssertionError(f'Missing export {path}\n{details}')
    lines = path.read_text(errors='replace').splitlines()
    assert sum(line.startswith('f ') for line in lines) >= minimum_faces, 'Export contains no mod geometry'
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


def check_signed_geometry(path, lower, upper, inward):
    vertices, faces = [], []
    for line in path.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields and fields[0] == 'f':
            faces.append([int(value.split('/')[0]) - 1 for value in fields[1:]])
    assert len(faces) == 6 and all(len(face) == 4 for face in faces)
    for axis in range(3):
        assert math.isclose(min(v[axis] for v in vertices), lower[axis], abs_tol=1e-6)
        assert math.isclose(max(v[axis] for v in vertices), upper[axis], abs_tol=1e-6)
    center = [(a + b) / 2 for a, b in zip(lower, upper)]
    for face in faces:
        assert all(0 <= index < len(vertices) for index in face)
        points = [vertices[index] for index in face]
        a = [points[1][i] - points[0][i] for i in range(3)]
        b = [points[2][i] - points[0][i] for i in range(3)]
        cross = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
        dot = sum(cross[i] * (points[0][i] - center[i]) for i in range(3))
        assert dot < 0 if inward else dot > 0, 'Signed model face winding changed'



def check_repeated_uv_geometry(path, individual):
    vertices, uv, faces = [], [], []
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == 'vt':
            uv.append(tuple(map(float, fields[1:3])))
        elif fields[0] == 'f':
            faces.append([tuple(int(x) - 1 for x in corner.split('/')[:2]) for corner in fields[1:]])
    for axis, (low, high) in enumerate(zip((.125, .1875, .25), (.875, .8125, .75))):
        assert math.isclose(min(v[axis] for v in vertices), low, abs_tol=1e-6)
        assert math.isclose(max(v[axis] for v in vertices), high, abs_tol=1e-6)
    area, count = 0., 0
    for face in faces:
        points = [vertices[v] for v, t in face]
        if not all(math.isclose(p[1], .8125, abs_tol=1e-6) for p in points):
            continue
        count += 1
        for i in range(1, len(points) - 1):
            a = [points[i][j] - points[0][j] for j in range(3)]
            b = [points[i+1][j] - points[0][j] for j in range(3)]
            cross_y = a[2]*b[0] - a[0]*b[2]
            assert cross_y > 0, 'Repeated UV face winding reversed or collapsed'
            area += cross_y / 2
        if individual:
            for vertex, texture in face:
                x, _, z = vertices[vertex]
                expected = (2 * (x - .125) / .75 - .5, 1 - (2 * (z - .25) / .5 - .5))
                for actual, wanted in zip(uv[texture], expected):
                    assert abs((actual - wanted + .5) % 1 - .5) < 1e-5, 'Repeated UV mapping changed'
    assert count > 1 and math.isclose(area, .375, abs_tol=1e-6), 'Repeated UV patches lost or duplicated surface area'


def exercise(executable, root):
    pack = FIXTURE.build_fixture(root)
    input_path = root / 'input.schem'
    names = schematic(input_path)
    world_path = root / 'world'
    world(world_path)
    catalog_manifest, catalog_input, catalog_names = large_catalog(root)
    assert gzip.decompress(input_path.read_bytes()).startswith(b'\x0a')
    manifest_text = (pack / 'manifest.mwpack').read_text(encoding='utf-8')
    assert manifest_text.splitlines()[0] in ('MINEWAYS_MOD_PACK\t1', 'MINEWAYS_MOD_PACK\t2', 'MINEWAYS_MOD_PACK\t3', 'MINEWAYS_MOD_PACK\t4', 'MINEWAYS_MOD_PACK\t5', 'MINEWAYS_MOD_PACK\t6')
    selectors = {line.split('\t')[1] for line in manifest_text.splitlines() if line.startswith('state\t')}
    assert {'ae2:matrix_frame', 'ae2:drive[facing=north,spin=0]', 'ae2:quartz_glass',
            'ae2:spatial_pylon', 'allthemodium:molten_allthemodium_block[level=0]',
            'allthemodium:molten_allthemodium_block[level=7]'} <= selectors, 'Adapter fixture is incomplete'
    assert '\ninvisible\n' in manifest_text, 'Invisible fixture marker is missing'
    assert 'fixture:wire[down=false,west=false]' not in selectors and 'fixture:wire' not in selectors
    if executable is None:
        return
    warning = run(executable, root, 'warning_summary', ['Center model: NO'])
    assert 'Warnings found in script file' in warning, 'Headless warning summary was not reported'
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
                                   'ae2:drive[facing=north,spin=1]',
                                   'ae2:drive[facing=east,spin=3]',
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
        if state == 'ae2:drive[facing=north,spin=1]':
            check_signed_geometry(output, (.375, 0, 0), (1, .75, 1), False)
        elif state == 'ae2:drive[facing=east,spin=3]':
            check_signed_geometry(output, (0, .25, 0), (1, 1, .625), False)
        verify_schematic(roundtrip, [state], details)
        assert list((root / (label + 'tex')).glob('*.png')), 'Adapter textures were not exported: ' + state
        if '[level=' in state:
            heights = [float(line.split()[2]) for line in output.read_text().splitlines()
                       if line.startswith('v ')]
            fluid_heights.append(max(heights) - min(heights))
    assert fluid_heights[0] > fluid_heights[1] > 0, 'Fluid levels lost their distinct surface heights'
    for label, lower, upper, inward in [
            ('inverted', (2/16, 3/16, 4/16), (14/16, 13/16, 12/16), True),
            ('extended', (-3, -2, -1), (3, 2.5, 2), False),
            ('complex', None, None, False)]:
        state = 'fixture:' + label + '[retained=true]'
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-roundtrip.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ' + label + 'tex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details)
        if label == 'complex':
            lines = output.read_text().splitlines()
            vertices = {tuple(map(float, line.split()[1:4])) for line in lines if line.startswith('v ')}
            expected = {(1-z/16, (2*y+dy)/16, (2*x+dx)/16)
                        for x in range(8) for y in range(8) for dx in (0, 1) for dy in (0, 1) for z in (2, 4)}
            assert vertices == expected and len(vertices) == 512, 'Complex model rotation lost vertices'
            assert sum(line.startswith('f ') for line in lines) == 384, 'Complex model lost faces'
        else:
            check_signed_geometry(output, lower, upper, inward)
        verify_schematic(restored, [state], details)
    mesh_points = {(-.25, 0, 0), (.75, 0, 0), (.25, 1, .5),
                   (0, 0, 1), (1, 0, 1), (1, .5, 1), (0, .5, 1)}
    texture_sources = {int(parts[1]): pack / parts[3]
                       for line in manifest_text.splitlines()
                       if (parts := line.split('\t'))[0] == 'texture'}
    for level in (0, 1):
        label = 'numeric-parts-' + str(level)
        state = f'fixture:numeric_parts[level={level}]'
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-restored.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory numerictex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details)
        check_signed_geometry(output, (.125, 0, .125), (.875, .25 if level == 0 else .75, .875), False)
        verify_schematic(restored, [state], details)
        assert check_export_pixels(output, texture_sources) == 1
    for facing in ('north', 'east'):
        label = 'simple-storage-' + facing
        state = 'fixture:simple_storage[facing=' + facing + ']'
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-restored.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory simplestorage',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details, minimum_faces=3)
        assert sum(line.startswith('f ') for line in output.read_text().splitlines()) == 3
        verify_schematic(restored, [state], details)
        assert check_export_pixels(output, texture_sources) == 1, 'Default storage base gained an overlay'
    for index, properties in enumerate(('facing=north,flat_top=false,vertical_facing=up',
                                        'facing=east,flat_top=false,vertical_facing=no',
                                        'facing=west,flat_top=true,vertical_facing=down')):
        label = 'limited-barrel-' + str(index)
        state = 'fixture:limited_barrel[' + properties + ']'
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-restored.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory limitedbarrel',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details, minimum_faces=4)
        assert sum(line.startswith('f ') for line in output.read_text().splitlines()) == 4
        verify_schematic(restored, [state], details)
        assert check_export_pixels(output, texture_sources) == 2, 'Barrel base/tier texture inheritance changed'
    for label, states, face_count in [
            ('material-north', ['fixture:material_block[facing=north]'], 2),
            ('material-east', ['fixture:material_block[facing=east]'], 2),
            ('material-empty-part', ['fixture:material_multipart[connected=false]'], 2),
            ('material-two-parts', ['fixture:material_multipart[connected=true]'], 4),
            ('material-invisible', ['fixture:material_block[facing=north]', 'fixture:material_block[facing=up]'], 2),
            ('transmitter-north', ['fixture:transmitter[facing=north]'], 6),
            ('transmitter-east', ['fixture:transmitter[facing=east]'], 6),
            ('io-machine-north', ['fixture:io_machine[facing=north]'], 3),
            ('io-machine-east', ['fixture:io_machine[facing=east]'], 3),
            ('wire-node', ['fixture:wire[down=true,west=false]'], 6),
            ('wire-elbow', ['fixture:wire[down=true,west=true]'], 18),
            ('rack-invisible', ['fixture:io_machine[facing=north]', 'minecolonies:blockminecoloniesrack[facing=north,variant=blockrackair]'], 3),
            ('framework-north', ['fixture:framework_open[facing=north]'], 6),
            ('framework-east', ['fixture:framework_open[facing=east]'], 6)]:
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-restored.schem')]
        schematic(source, states)
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ' + label + 'tex',
                      f'Selection location min to max: 0, 0, 0 to {len(states)-1}, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details, minimum_faces=face_count)
        assert sum(line.startswith('f ') for line in output.read_text().splitlines()) == face_count, label
        verify_schematic(restored, states, details)
        assert check_export_pixels(output, texture_sources) == 1, 'Default model gained an overlay: ' + label
        if label.startswith('transmitter-'):
            check_signed_geometry(output, (.25, .25, .25), (.75, .75, .75), False)
        elif label in ('material-north', 'material-empty-part', 'material-invisible'):
            lines = output.read_text().splitlines()
            points = [tuple(map(float, line.split()[1:4])) for line in lines if line.startswith('v ')]
            vertices = {points[int(corner.split('/')[0])-1] for line in lines if line.startswith('f ') for corner in line.split()[1:]}
            expected = {(x/16, y/16, 3/16) for x in (1, 12) for y in (2, 14)}
            expected.update((x/16, 14/16, z/16) for x in (1, 12) for z in (3, 15))
            assert vertices == expected, 'Material parent coordinates changed: ' + label
        elif label.startswith('framework-'):
            angle = math.radians(11.31)
            expected = []
            for x in (2, 13):
                for y in (3, 11):
                    for z in (4, 14):
                        point = (x/16, (5+(y-5)*math.cos(angle)-(z-6)*math.sin(angle))/16,
                                 (6+(y-5)*math.sin(angle)+(z-6)*math.cos(angle))/16)
                        expected.append((1-point[2], point[1], point[0]) if label.endswith('east') else point)
            points = [tuple(map(float, line.split()[1:4])) for line in output.read_text().splitlines() if line.startswith('v ')]
            assert len(points) == len(expected) == 8
            assert all(any(all(abs(a-b) < 1e-6 for a,b in zip(point, corner)) for corner in expected) for point in points), label
    exercise_custom_defaults(executable, root, common, texture_sources)
    for orientation in ('plain', 'turned'):
        label = 'mesh-' + orientation
        state = 'fixture:obj_mesh[orientation=' + orientation + ',retained=true]'
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-roundtrip.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ' + label + 'tex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details, minimum_faces=2)
        lines = output.read_text().splitlines()
        vertices = {tuple(map(float, line.split()[1:4])) for line in lines if line.startswith('v ')}
        expected = mesh_points if orientation == 'plain' else {(y, z, x) for x, y, z in mesh_points}
        assert vertices == expected, 'OBJ polygon coordinates or blockstate rotation changed'
        faces = [line.split()[1:] for line in lines if line.startswith('f ')]
        assert sorted(map(len, faces)) == [3, 4], 'OBJ triangle/quad topology changed'
        verify_schematic(restored, [state], details)
        assert check_export_pixels(output, texture_sources) == 2
        usd = root / (label + '.usda')
        details = run(executable, root, label + '-usd', [*common, 'Minecraft world: ' + str(source),
                      'Set render type: USD 1.0',
                      'File type: Export individual textures to directory ' + label + 'usdtex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(usd)])
        assert usd.exists() and usd.stat().st_size > 100, details
    uv_source = root / 'uv-repeat.schem'
    uv_state = 'fixture:uv_repeat[orientation=plain,retained=true]'
    schematic(uv_source, [uv_state])
    for individual in (True, False):
        label = 'uv-repeat-' + ('individual' if individual else 'mosaic')
        output = root / (label + '.obj')
        restored = root / (label + '.schem')
        texture_mode = ('Export individual textures to directory uvrepeattex' if individual
                        else 'Export full color texture patterns')
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(uv_source),
                      'File type: ' + texture_mode,
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details)
        check_repeated_uv_geometry(output, individual)
        verify_schematic(restored, [uv_state], details)
        if individual:
            check_export_pixels(output, texture_sources)
    cloud_names = ['fixture:cloud_' + name + '[retained=true]'
                   for name in ('blue', 'green', 'translucent', 'intense')]
    cloud_states = [state for i, name in enumerate(cloud_names)
                    for state in ([name] if i == 0 else ['minecraft:air', name])]
    cloud_source = root / 'cloud-input.schem'
    cloud_restored = root / 'cloud-roundtrip.schem'
    schematic(cloud_source, cloud_states)
    cloud_outputs = []
    for label, source in [('cloud', cloud_source), ('cloud-roundtrip', cloud_restored)]:
        output = root / (label + '.obj')
        commands = [*common, 'Minecraft world: ' + str(source),
                    'File type: Export individual textures to directory cloudtex',
                    'Selection location min to max: 0, 0, 0 to 6, 0, 0',
                    'Export for Rendering: ' + str(output)]
        if label == 'cloud':
            commands.append('Export Schematic: ' + str(cloud_restored))
        details = run(executable, root, label, commands)
        check_obj(output, details, minimum_faces=24)
        assert check_export_pixels(output, texture_sources) == 4
        cloud_outputs.append(obj_geometry(output))
    assert cloud_outputs[0] == cloud_outputs[1], 'CloudFX Sponge geometry changed'
    verify_schematic(cloud_restored, cloud_states, details)
    ie_cases = [
        ('fixture:ie_chute', (2, 0, 2), (14, 4, 14)),
        ('fixture:ie_lantern[facing=up]', (6, 0, 6), (10, 12, 10)),
        ('fixture:ie_balloon', (0, 2, 2), (12, 14, 14)),
        ('fixture:ie_floodlight[facing=down]', (4, 2, 4), (12, 10, 12)),
        ('fixture:ie_floodlight[facing=up]', (4, 6, 4), (12, 14, 12)),
        ('fixture:ie_slope', (0, 0, 0), (16, 16, 16))]
    for index, (state, lower, upper) in enumerate(ie_cases):
        label = 'ie-obj-' + str(index)
        source, output = root / (label + '.schem'), root / (label + '.obj')
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ietex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output)])
        check_obj(output, details)
        points = [tuple(map(float, line.split()[1:4])) for line in output.read_text().splitlines() if line.startswith('v ')]
        for axis in range(3):
            assert math.isclose(min(v[axis] for v in points), lower[axis] / 16, abs_tol=1e-6), label
            assert math.isclose(max(v[axis] for v in points), upper[axis] / 16, abs_tol=1e-6), label
        check_export_pixels(output, texture_sources)
    for kind in ('dummy', 'position'):
        prefix = 'ie-split-' + kind
        split_source, split_restored = root / (prefix + '.schem'), root / (prefix + '-restored.schem')
        split_state = 'fixture:ie_split' + ('_position' if kind == 'position' else '') + '[retained=true]'
        split_entities = [({'id': 'immersiveengineering:silo_dummy', 'pos': (0, y, 0), 'position': (1, y, 1)}
                           if kind == 'position' else {'id': 'immersiveengineering:cloche', 'pos': (0, y, 0), 'dummy': y})
                          for y in range(3)]
        schematic(split_source, [split_state], [0, 0, 0], (1, 3, 1), split_entities)
        split_geometry = []
        for label, source in [(prefix, split_source), (prefix + '-restored', split_restored)]:
            output = root / (label + '.obj')
            commands = [*common, 'Minecraft world: ' + str(source),
                        'File type: Export individual textures to directory iesplittex',
                        'Selection location min to max: 0, 0, 0 to 0, 2, 0',
                        'Export for Rendering: ' + str(output)]
            if label == prefix:
                commands.append('Export Schematic: ' + str(split_restored))
            details = run(executable, root, label, commands)
            check_obj(output, details, minimum_faces=3)
            split_geometry.append(obj_geometry(output))
            check_export_pixels(output, texture_sources)
        assert split_geometry[0] == split_geometry[1], 'Saved IE offsets changed in Sponge round trip'
        raw = gzip.decompress(split_restored.read_bytes())
        assert split_entities[0]['id'].encode() in raw
        assert raw.count(b'posInMB' if kind == 'position' else b'dummy') == 3
        verify_schematic(split_restored, [split_state], details)
    missing_source, missing_output = root / 'ie-missing.schem', root / 'ie-missing.obj'
    schematic(missing_source, [split_state])
    details = run(executable, root, 'ie-missing', [*common, 'Minecraft world: ' + str(missing_source),
                  'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                  'Export for Rendering: ' + str(missing_output)], expect_success=False)
    assert 'needs saved block-entity data' in details and not missing_output.exists(), details
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




def obj_geometry(path):
    return [line for line in path.read_text(errors='replace').splitlines()
            if line.startswith(('v ', 'vt ', 'vn ', 'f ', 'usemtl '))]


def read_textured_faces(path):
    materials, current = {}, None
    for line in path.with_suffix('.mtl').read_text().splitlines():
        if line.startswith('newmtl '):
            current = line[7:]
        elif line.startswith('map_Kd '):
            texture = path.parent / line[7:].replace('\\', '/')
            materials[current] = FIXTURE.mod.decode_png(texture.read_bytes())
    points, uv, faces = [], [], []
    for line in path.read_text().splitlines():
        fields = line.split()
        if line.startswith('v '):
            points.append(tuple(map(float, fields[1:4])))
        elif line.startswith('vt '):
            uv.append(tuple(map(float, fields[1:3])))
        elif line.startswith('usemtl '):
            current = line[7:]
        elif line.startswith('f '):
            corners = [tuple(map(int, value.split('/')[:2])) for value in fields[1:]]
            faces.append(([(points[p-1], uv[t-1]) for p, t in corners], materials[current]))
    return faces


def exercise_custom_defaults(executable, root, common, texture_sources):
    samples = [(f'framed-{f}', f'fixture:framed_default[facing={f}]', 3, 2) for f in ('north', 'east')]
    samples += [(f'microchip-{f}', f'fixture:microchip_default[facing={f}]', 6, 2) for f in ('north', 'east')]
    samples += [(f'lantern-{f}', f'amendments:wall_lantern[attachment=block,facing={f}]', 10, 2)
                for f in ('north', 'east', 'south', 'west')]
    for kind, textures in (('side_top_bottom', 3), ('side_vertical', 2), ('vertical', 2), ('all_same_texture', 1)):
        samples += [(f'ie-sides-{kind}-{turn}', f'fixture:ie_sides_{kind}[orientation={turn}]', 6, textures)
                    for turn in ('plain', 'turned')]
    references = {}
    for label, state, face_count, texture_count in samples:
        source, output, restored = [root / (label + suffix) for suffix in ('.schem', '.obj', '-restored.schem')]
        schematic(source, [state])
        details = run(executable, root, label, [*common, 'Minecraft world: ' + str(source),
                      'File type: Export individual textures to directory ' + label + 'tex',
                      'Selection location min to max: 0, 0, 0 to 0, 0, 0',
                      'Export for Rendering: ' + str(output), 'Export Schematic: ' + str(restored)])
        check_obj(output, details, minimum_faces=face_count)
        assert sum(line.startswith('f ') for line in output.read_text().splitlines()) == face_count, label
        assert check_export_pixels(output, texture_sources) == texture_count, label
        verify_schematic(restored, [state], details)
        if label.startswith('lantern-'):
            turns = ('north', 'east', 'south', 'west').index(label[8:])
            points = [tuple(map(float, line.split()[1:4])) for line in output.read_text().splitlines() if line.startswith('v ')]
            for _ in range((4-turns) % 4):
                points = [(1-z, y, x) for x, y, z in points]
            assert all(abs(min(p[i] for p in points)-v) < 1e-6 for i, v in enumerate((1/16, 3/16, 7/16)))
            assert all(abs(max(p[i] for p in points)-v) < 1e-6 for i, v in enumerate((15/16, 15/16, 1)))
        if not label.startswith(('microchip-', 'ie-sides-')):
            continue
        check_signed_geometry(output, (0, 0, 0), (1, 1, 1), False)
        faces = read_textured_faces(output)
        key = label.rsplit('-', 1)[0]
        if key in references:
            assert faces == references[key], 'Generated loader incorrectly inherited application rotation: ' + label
        else:
            references[key] = faces
        for corners, (width, height, pixels) in faces:
            center = [sum(p[i] for p, _ in corners)/4 for i in range(3)]
            side = next(name for axis, name0, name1 in ((1, 'down', 'up'), (2, 'north', 'south'), (0, 'west', 'east'))
                        for value, name in ((0, name0), (1, name1)) if center[axis] == value)
            for a in (.25, .75):
                for b in (.25, .75):
                    weights = ((1-a)*(1-b), a*(1-b), a*b, (1-a)*b)
                    point = [sum(w*p[i] for w, (p, _) in zip(weights, corners)) for i in range(3)]
                    uv = [sum(w*t[i] for w, (_, t) in zip(weights, corners)) for i in range(2)]
                    offset = (min(height-1, int((1-uv[1])*height))*width + min(width-1, int(uv[0]*width)))*4
                    if label.startswith('microchip-'):
                        expected = (180, 60, 185, 255) if side == 'up' else (95, 175, 45, 255)
                    else:
                        x, y, z = point
                        u, v = {'down': (x, 1-z), 'up': (x, z), 'north': (1-x, 1-y),
                                'south': (x, 1-y), 'west': (z, 1-y), 'east': (1-z, 1-y)}[side]
                        kind = key[len('ie-sides-'):]
                        blue = (40 if side == 'down' else 90) if kind == 'side_top_bottom' and side in ('down', 'up') else (
                            90 if side in ('down', 'up') and kind != 'all_same_texture' else 200 if kind == 'vertical' else 150)
                        expected = (min(15, int(u*16))*16, min(15, int(v*16))*16, blue, 255)
                    assert tuple(pixels[offset:offset+4]) == expected, (label, side, point, expected)


def check_export_pixels(object_path, sources):
    """Compare every exported imported texel with its source's nearest-neighbor sample."""
    material = object_path.with_suffix('.mtl')
    checked = set()
    for line in material.read_text(errors='replace').splitlines():
        if not line.startswith('map_Kd '):
            continue
        path = object_path.parent / line[7:].replace('\\', '/')
        if not path.stem.startswith('mw_mod_') or path in checked:
            continue
        index = int(path.stem[7:])
        assert index in sources, 'Export references an absent texture: ' + str(index)
        width, height, actual = FIXTURE.mod.decode_png(path.read_bytes())
        sw, sh, original = FIXTURE.mod.decode_png(sources[index].read_bytes())
        for y in range(height):
            for x in range(width):
                target = (y * width + x) * 4
                source = ((y * sh // height) * sw + x * sw // width) * 4
                assert actual[target:target + 4] == original[source:source + 4], str(path)
        checked.add(path)
    assert checked, 'No imported texture pixels were checked'
    return len(checked)


def exercise_pack(executable, root, manifest):
    """Use an external compiled pack, never a user's live Minecraft world."""
    rotation_blocks = {
        'advanced_ae:quantum_crafter',
        'ae2:charger',
        'ae2:chest',
        'ae2:drive',
        'ae2:inscriber',
        'ae2:io_port',
        'ae2:spatial_io_port',
        'ae2:vibration_chamber',
        'expandedae:exp_io_port',
        'extendedae:caner',
        'extendedae:circuit_cutter',
        'extendedae:crystal_assembler',
        'extendedae:ex_charger',
        'extendedae:ex_drive',
        'extendedae:ex_inscriber',
        'extendedae:ex_io_port',
    }
    rotation_states = []
    by_block, textures, split_policies = {}, {}, {}
    current_policy = None
    selectors = 0
    with manifest.open(encoding='utf-8') as stream:
        for line in stream:
            fields = line.rstrip('\r\n').split('\t')
            if fields[0] == 'state':
                state = fields[1]
                current_policy = None
                block = state.split('[', 1)[0]
                by_block.setdefault(block, [state, state])[1] = state
                if block in rotation_blocks:
                    rotation_states.append(state)
                selectors += 1
            elif fields[0] == 'splitpolicy':
                current_policy = {'kind': fields[1], 'id': fields[2],
                                  'master': tuple(map(int, fields[3:6])), 'size_x': int(fields[6])}
                split_policies[state] = current_policy
            elif fields[0] == 'part' and current_policy is not None:
                current_policy.setdefault('part', tuple(map(int, fields[1:4])))
            elif fields[0] == 'texture':
                path = (manifest.parent / fields[3]).resolve()
                assert manifest.parent.resolve() in path.parents, 'Texture escapes pack root'
                textures[int(fields[1])] = path
    assert by_block and textures, 'External pack has no renderable catalog'
    selected = list(dict.fromkeys(rotation_states))
    blocks = sorted(by_block)
    step = max(1, (len(blocks) + 63) // 64)
    for block in blocks[::step]:
        for state in by_block[block]:
            if state not in selected:
                selected.append(state)
    targets = ['ae2:matrix_frame', 'ae2:drive', 'ae2:quartz_glass',
               'ae2:quartz_vibrant_glass', 'ae2:spatial_pylon',
               'handcrafted:acacia_bench', 'handcrafted:blue_bowl',
               'botanypots:black_concrete_botany_pot', 'bibliocraft:black_fancy_gold_lamp',
               'refinedstorage:cable', 'cabletiers:black_creative_constructor',
               'bibliocraft:acacia_table', 'bibliocraft:acacia_bookcase',
               'xycraft_machines:machine_base', 'xycraft_override:smooth_polished_tuff',
               'advancedperipherals:block_reader', 'waystones:blackstone_waystone',
               'refurbished_furniture:acacia_desk',
               'modern_industrialization:assembler',
               'modern_industrialization:bronze_furnace',
               'modern_industrialization:configurable_tank',
               'extended_industrialization:large_steel_crate',
               'industrialization_overdrive:multi_processing_array',
               'refinedstorage:disk_interface',
               'refinedstorage:disk_drive',
               'cabletiers:black_creative_disk_interface',
               'extendedae:ex_drive',
               'supplementaries:awning',
               'supplementaries:awning_red',
               'create:crushing_wheel',
               'create:large_water_wheel',
               'neovitae:raw_spiritus_crystal',
               'xycraft_machines:collector',
               'xycraft_machines:energy_selector',
               'xycraft_machines:stake',
               'create:creative_motor',
               'handcrafted:pillager_trophy',
               'handcrafted:stackable_book',
               'immersiveengineering:wall_clinker_brick',
               'mcwfurnitures:acacia_glass_table',
               'mcwroofs:grass_steep_roof',
               'mcwroofs:black_striped_awning',
               'mekanism:chemical_oxidizer',
               'neovitae:rune_acceleration',
               'oritech:black_hole_block',
               'pneumaticcraft:programmer',
               'refurbished_furniture:black_trampoline',
               'silentgear:material_grader',
               'allthecompressed:xychorium_storage_blue_1x',
               'allthecompressed:xychorium_storage_red_9x',
               'xycraft_world:aluminum_bricks_cloud_blue',
               'xycraft_world:xychorium_storage_blue',
               'xycraft_world:lamp_pillar_blue',
               'xycraft_world:lamp_lantern_blue',
               'xycraft_machines:buffer',
               'xycraft_machines:engineering_table',
               'xycraft_machines:extractor',
               'xycraft_machines:isolator',
               'xycraft_machines:nitrogen_extractor',
               'xycraft_override:bricks_red',
               'immersiveengineering:chute_aluminum',
               'immersiveengineering:chute_colored_red',
               'immersiveengineering:lantern',
               'immersiveengineering:cagelamp',
               'immersiveengineering:balloon',
               'immersiveengineering:siren',
               'immersiveengineering:logic_unit',
               'immersiveengineering:fluid_pipe',
               'immersiveengineering:alu_slope',
               'immersiveengineering:steel_slope',
               'immersiveengineering:breaker_switch',
               'immersiveengineering:redstone_breaker',
               'immersiveengineering:redstone_timer',
               'immersiveengineering:connector_probe',
               'immersiveengineering:connector_redstone',
               'immersiveengineering:connector_structural',
               'immersiveengineering:floodlight',
               'immersiveengineering:turret_gun',
               'immersiveengineering:turret_chem',
               'sophisticatedstorage:acacia_storage_connector',
               'sophisticatedstorage:bamboo_storage_connector',
               'sophisticatedstorage:birch_storage_connector',
               'sophisticatedstorage:cherry_storage_connector',
               'sophisticatedstorage:controller',
               'sophisticatedstorage:crimson_storage_connector',
               'sophisticatedstorage:dark_oak_storage_connector',
               'sophisticatedstorage:jungle_storage_connector',
               'sophisticatedstorage:limited_barrel_1',
               'sophisticatedstorage:limited_barrel_2',
               'sophisticatedstorage:limited_barrel_3',
               'sophisticatedstorage:limited_barrel_4',
               'sophisticatedstorage:limited_copper_barrel_1',
               'sophisticatedstorage:limited_copper_barrel_2',
               'sophisticatedstorage:limited_copper_barrel_3',
               'sophisticatedstorage:limited_copper_barrel_4',
               'sophisticatedstorage:limited_diamond_barrel_1',
               'sophisticatedstorage:limited_diamond_barrel_2',
               'sophisticatedstorage:limited_diamond_barrel_3',
               'sophisticatedstorage:limited_diamond_barrel_4',
               'sophisticatedstorage:limited_gold_barrel_1',
               'sophisticatedstorage:limited_gold_barrel_2',
               'sophisticatedstorage:limited_gold_barrel_3',
               'sophisticatedstorage:limited_gold_barrel_4',
               'sophisticatedstorage:limited_iron_barrel_1',
               'sophisticatedstorage:limited_iron_barrel_2',
               'sophisticatedstorage:limited_iron_barrel_3',
               'sophisticatedstorage:limited_iron_barrel_4',
               'sophisticatedstorage:limited_netherite_barrel_1',
               'sophisticatedstorage:limited_netherite_barrel_2',
               'sophisticatedstorage:limited_netherite_barrel_3',
               'sophisticatedstorage:limited_netherite_barrel_4',
               'sophisticatedstorage:mangrove_storage_connector',
               'sophisticatedstorage:oak_storage_connector',
               'sophisticatedstorage:spruce_storage_connector',
               'sophisticatedstorage:storage_input',
               'sophisticatedstorage:storage_io',
               'sophisticatedstorage:storage_link',
               'sophisticatedstorage:storage_output',
               'sophisticatedstorage:warped_storage_connector',
               'herbsandharvest:cake_block',
               'merrymaking:kinara',
               'merrymaking:topper',
               'supplementaries:crank',
               'supplementaries:rope_tomatoes',
               'supplementaries:stick_tomatoes',
               'securitycraft:reinforced_scaffolding',
               'amendments:forbidden_arcanus/wall_deorum_lantern',
               'amendments:forbidden_arcanus/wall_deorum_soul_lantern',
               'amendments:wall_lantern',
               'amendments:wall_soul_lantern',
               'functionalstorage:compacting_framed_drawer',
               'functionalstorage:framed_1',
               'functionalstorage:framed_2',
               'functionalstorage:framed_4',
               'functionalstorage:framed_controller_extension',
               'functionalstorage:framed_fluid_1',
               'functionalstorage:framed_fluid_2',
               'functionalstorage:framed_fluid_4',
               'functionalstorage:framed_simple_compacting_drawer',
               'functionalstorage:framed_storage_controller',
               'immersiveengineering:capacitor_creative',
               'immersiveengineering:capacitor_hv',
               'immersiveengineering:capacitor_lv',
               'immersiveengineering:capacitor_mv',
               'immersiveengineering:fluid_placer',
               'immersiveengineering:fluid_pump',
               'immersiveengineering:metal_barrel',
               'immersiveengineering:wooden_barrel',
               'little_big_redstone:black_microchip',
               'little_big_redstone:blue_microchip',
               'little_big_redstone:brown_microchip',
               'little_big_redstone:cyan_microchip',
               'little_big_redstone:gray_microchip',
               'little_big_redstone:green_microchip',
               'little_big_redstone:light_blue_microchip',
               'little_big_redstone:light_gray_microchip',
               'little_big_redstone:lime_microchip',
               'little_big_redstone:magenta_microchip',
               'little_big_redstone:orange_microchip',
               'little_big_redstone:pink_microchip',
               'little_big_redstone:purple_microchip',
               'little_big_redstone:red_microchip',
               'little_big_redstone:white_microchip',
               'little_big_redstone:yellow_microchip',
               'enderio:alloy_smelter',
               'enderio:crafter',
               'enderio:drain',
               'enderio:enchanter',
               'enderio:fluid_tank',
               'enderio:impulse_hopper',
               'enderio:niard',
               'enderio:painting_machine',
               'enderio:powered_spawner',
               'enderio:pressurized_fluid_tank',
               'enderio:sag_mill',
               'enderio:slice_and_splice',
               'enderio:soul_binder',
               'enderio:soul_engine',
               'enderio:stirling_generator',
               'enderio:vat',
               'enderio:wired_charger',
               'enderio:wireless_charger',
               'refurbished_furniture:andesite_stepping_stones',
               'refurbished_furniture:computer',
               'refurbished_furniture:dark_ceiling_light',
               'refurbished_furniture:dark_range_hood',
               'refurbished_furniture:deepslate_stepping_stones',
               'refurbished_furniture:diorite_stepping_stones',
               'refurbished_furniture:frying_pan',
               'refurbished_furniture:granite_stepping_stones',
               'refurbished_furniture:light_ceiling_light',
               'refurbished_furniture:light_range_hood',
               'refurbished_furniture:stone_stepping_stones',
               'minecolonies:blockminecoloniesrack',
               'mekanism:advanced_logistical_transporter',
               'mekanism:advanced_mechanical_pipe',
               'mekanism:advanced_pressurized_tube',
               'mekanism:advanced_thermodynamic_conductor',
               'mekanism:advanced_universal_cable',
               'mekanism:basic_logistical_transporter',
               'mekanism:basic_mechanical_pipe',
               'mekanism:basic_pressurized_tube',
               'mekanism:basic_thermodynamic_conductor',
               'mekanism:basic_universal_cable',
               'mekanism:diversion_transporter',
               'mekanism:elite_logistical_transporter',
               'mekanism:elite_mechanical_pipe',
               'mekanism:elite_pressurized_tube',
               'mekanism:elite_thermodynamic_conductor',
               'mekanism:elite_universal_cable',
               'mekanism:restrictive_transporter',
               'mekanism:ultimate_logistical_transporter',
               'mekanism:ultimate_mechanical_pipe',
               'mekanism:ultimate_pressurized_tube',
               'mekanism:ultimate_thermodynamic_conductor',
               'mekanism:ultimate_universal_cable']
    targets += [name for name in blocks if name.startswith(('allthemodium:molten_', 'domum_ornamentum:', 'morered:'))]
    targets += sorted({state.split('[', 1)[0] for state in split_policies})
    for block in targets:
        if block in by_block:
            for state in by_block[block]:
                if state not in selected:
                    selected.append(state)
    observed = []
    for state in selected:
        policy = split_policies.get(state)
        if policy and policy['kind'] in ('post', 'slave_x', 'slave_y', 'slave_z', 'meter'):
            name, _, raw = state.partition('[')
            props = dict(item.split('=', 1) for item in raw.rstrip(']').split(',') if item)
            if policy['kind'] == 'post':
                props.setdefault('post_slave', '0')
                props.setdefault('horizontal_offset', 'none')
            else:
                props.setdefault('multiblockslave', 'false')
            state = name + '[' + ','.join(key + '=' + value for key, value in sorted(props.items())) + ']'
            split_policies[state] = policy
        observed.append(state)
    selected = list(dict.fromkeys(observed))
    names = []
    for state in selected:
        if names:
            names.append('minecraft:air')
        names.append(state)
    source = root / 'actual-input.schem'
    # Activate every installed block name, then render a compact grid to retain tiny source details.
    # Unused palette entries occupy no voxels. No live world is opened.
    palette = list(dict.fromkeys([by_block[block][0] for block in blocks] + names))
    assert len(palette) <= 65536, 'Full-pack test exceeds the Sponge palette limit'
    palette_index = {name: index for index, name in enumerate(palette)}
    columns = min(32, len(selected))
    sample_width = columns * 2 - 1
    sample_length = ((len(selected) + columns - 1) // columns) * 2 - 1
    width = 512
    length = max(sample_length, (len(palette) + width - 1) // width)
    indices = [palette_index['minecraft:air']] * (width * length)
    for index, state in enumerate(selected):
        indices[(index % columns) * 2 + (index // columns) * 2 * width] = palette_index[state]
    entities = []
    for index, state in enumerate(selected):
        policy = split_policies.get(state)
        if not policy or policy['kind'] not in ('dummy', 'position'):
            continue
        part = policy.get('part', (0, 0, 0))
        entry = {'id': policy['id'], 'pos': ((index % columns) * 2, 0, (index // columns) * 2)}
        if policy['kind'] == 'dummy':
            entry['dummy'] = part[1]
        else:
            assert entry['id'].endswith('_master')
            entry['id'] = entry['id'][:-7] + '_dummy'
            position = [part[i] + policy['master'][i] for i in range(3)]
            if 'mirrored=true' in state:
                position[0] = policy['size_x'] - position[0] - 1
            entry['position'] = position
        entities.append(entry)
    schematic(source, palette, indices, (width, 1, length), entities)
    (root / 'selected-states.txt').write_text('\n'.join(selected) + '\n', encoding='utf-8')
    print(f'External catalog: {len(blocks)} blocks, {selectors} selectors, {len(textures)} textures; {len(selected)} sampled states', flush=True)
    common = ['Mod pack: ' + str(manifest),
              'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
              'Export lesser blocks: YES', 'Individual blocks: YES',
              'File type: Export individual textures to directory actualtex']
    selection = f'Selection location min to max: 0, 0, 0 to {sample_width - 1}, 0, {sample_length - 1}'
    obj, repeated, roundtrip = [root / name for name in ['actual.obj', 'actual-repeat.obj', 'actual.schem']]
    after = root / 'actual-roundtrip.obj'
    actual_commands = [*common, 'Minecraft world: ' + str(source), selection,
                       'Export for Rendering: ' + str(obj), 'Export for Rendering: ' + str(repeated),
                       'Export Schematic: ' + str(roundtrip)]
    roundtrip_commands = [*common, 'Minecraft world: ' + str(roundtrip), selection,
                          'Export for Rendering: ' + str(after)]
    if executable is None:
        # Build and validate the same scripts as the native route before skipping execution.
        write_script(root, 'actual', actual_commands)
        write_script(root, 'actual_roundtrip', roundtrip_commands)
        return
    details = run(executable, root, 'actual', actual_commands, timeout=600)
    check_obj(obj, details)
    verify_schematic(roundtrip, selected, details)
    assert obj_geometry(obj) == obj_geometry(repeated), 'Repeated real-pack export changed geometry'
    checked = check_export_pixels(obj, textures)
    details = run(executable, root, 'actual_roundtrip', roundtrip_commands, timeout=600)
    check_obj(after, details)
    assert obj_geometry(obj) == obj_geometry(after), 'Real-pack Sponge roundtrip changed geometry'
    print(f'PASS: external pack, {len(selected)} sampled states, {checked} pixel-checked textures, repeated OBJ and Sponge geometry', flush=True)


def check_mosaic(path, expected_blocks):
    material = path.with_suffix('.mtl').read_text(errors='replace').splitlines()
    image_name = next(line[7:] for line in material if line.startswith('map_Kd '))
    width, height, pixels = FIXTURE.mod.decode_png((path.parent / image_name.replace('\\', '/')).read_bytes())
    vertices, uv = [], []
    faces = 0
    for line in path.read_text(errors='replace').splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == 'v':
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == 'vt':
            uv.append(tuple(map(float, fields[1:3])))
        elif fields[0] == 'f':
            indices = [tuple(map(int, entry.split('/'))) for entry in fields[1:]]
            assert len(indices) == 4
            block = math.floor(sum(vertices[vertex - 1][0] for vertex, _, _ in indices) / 8)
            assert 0 <= block < expected_blocks
            u = sum(uv[texture - 1][0] for _, texture, _ in indices) / 4
            v = sum(uv[texture - 1][1] for _, texture, _ in indices) / 4
            x, y = min(width - 1, int(u * width)), min(height - 1, int((1 - v) * height))
            offset = (y * width + x) * 4
            assert pixels[offset:offset + 4] == bytes((block & 255, block >> 8, 73, 255)), (block, x, y)
            faces += 1
    assert faces == expected_blocks * 6


def exercise_limits(executable, root):
    pack = FIXTURE.build_limits_fixture(root / 'limits')
    manifest = pack / 'manifest.mwpack'
    # Resolve every palette entry, even entries unused by these five voxels. This activates all IDs.
    palette = [f'wide:block_{index}' for index in range(65536 - 561)]
    boundaries = [4095, 4096, 32767, 32768, 65535]
    palette += [f'wide:block_64974[extra={state}]' for state in range(1, 258)]
    palette.append('minecraft:air')
    indices = []
    for block in boundaries:
        if indices:
            indices.append(len(palette) - 1)
        indices.append(block - 561)
    source = root / 'wide-input.schem'
    indices[-1] = len(palette) - 2
    # The NBT reader bounds palette entries by voxel count. Fill unused space with air.
    indices += [len(palette) - 1] * (65536 - len(indices))
    schematic(source, palette, indices, (256, 1, 256))
    # Separated cubes avoid making culling correctness part of the texture capacity test.
    selected = ['minecraft:air'] + [f'wide:block_{i}' for i in range(4097)]
    voxels = []
    for i in range(1, 4097):
        if voxels:
            voxels.append(0)
        voxels.append(i)
    texture_source = root / '4096-input.schem'
    schematic(texture_source, selected[:-1], voxels)
    overflow_source = root / '4097-input.schem'
    schematic(overflow_source, selected, voxels + [0, 4097])
    if executable is None:
        return
    common = ['Mod pack: ' + str(manifest),
              'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
              'Export lesser blocks: YES', 'Individual blocks: YES',
              'File type: Export individual textures to directory widetex']
    obj, exported = root / 'wide.obj', root / 'wide.schem'
    details = run(executable, root, 'wide', [*common, 'Minecraft world: ' + str(source),
                  'Selection location min to max: 0, 0, 0 to 8, 0, 0',
                  'Export for Rendering: ' + str(obj), 'Export Schematic: ' + str(exported)], timeout=600)
    check_obj(obj, details)
    assert sum(line.startswith('f ') for line in obj.read_text().splitlines()) == 30
    expected = [f'wide:block_{i - 561}' for i in boundaries[:-1]] + ['wide:block_64974[extra=257]']
    verify_schematic(exported, expected, details)
    usd = root / 'wide.usda'
    details = run(executable, root, 'wide_usd', [*common, 'Minecraft world: ' + str(source),
                  'Set render type: USD 1.0', 'Individual blocks: YES',
                  'File type: Export individual textures to directory wideusdtex',
                  'Selection location min to max: 0, 0, 0 to 8, 0, 0',
                  'Export for Rendering: ' + str(usd)], timeout=600)
    assert usd.exists(), details
    assert 'three-texture output option' not in details, 'Individual USD textures produced a mosaic warning'
    library = root / 'wide_materials' / 'BlockLibrary.usda'
    content = library.read_text(errors='replace')
    for block in boundaries:
        state = 257 if block == 65535 else 0
        assert f'def Xform "Block_{block}_{state}"' in content, 'USD instance type/state truncated'
    assert content.count('    def Xform ') == 5
    tiles, repeat, sponge = root / '4096.obj', root / '4096-repeat.obj', root / '4096.schem'
    details = run(executable, root, '4096', [*common, 'Minecraft world: ' + str(texture_source),
                  'Selection location min to max: 0, 0, 0 to 8190, 0, 0',
                  'Export for Rendering: ' + str(tiles), 'Export for Rendering: ' + str(repeat),
                  'Export Schematic: ' + str(sponge)], timeout=600)
    check_obj(tiles, details)
    assert sum(line.startswith('f ') for line in tiles.read_text().splitlines()) == 4096 * 6
    assert obj_geometry(tiles) == obj_geometry(repeat)
    assert sum(line.startswith('vt ') for line in tiles.read_text().splitlines()) > 32767
    verify_schematic(sponge, selected[1:4097], details)
    sources = {i: pack / 'textures' / f'{i}.png' for i in range(4097)}
    assert check_export_pixels(tiles, sources) == 4096
    restored = root / '4096-roundtrip.obj'
    details = run(executable, root, '4096_roundtrip', [*common, 'Minecraft world: ' + str(sponge),
                  'Selection location min to max: 0, 0, 0 to 8190, 0, 0',
                  'Export for Rendering: ' + str(restored)], timeout=600)
    check_obj(restored, details)
    assert obj_geometry(tiles) == obj_geometry(restored), '4096-texture Sponge roundtrip changed geometry'
    mosaic = root / '4096-mosaic.obj'
    details = run(executable, root, '4096_mosaic', [*common, 'Minecraft world: ' + str(texture_source),
                  'File type: Export full color texture patterns',
                  'Selection location min to max: 0, 0, 0 to 8190, 0, 0',
                  'Export for Rendering: ' + str(mosaic)], timeout=600)
    check_obj(mosaic, details)
    check_mosaic(mosaic, 4096)
    overflow = root / '4097.obj'
    run(executable, root, '4097', [*common, 'Minecraft world: ' + str(overflow_source),
        'Selection location min to max: 0, 0, 0 to 8192, 0, 0',
        'Export for Rendering: ' + str(overflow)], expect_success=False, timeout=600)
    assert not overflow.exists(), 'A 4097-texture selection bypassed its explicit budget'
    print('PASS: 64,975 active names, uint16 boundary IDs, 4096 texture pixels, explicit 4097 failure', flush=True)

def exercise_normals(executable, root):
    """Export 800 distinct mesh normals, beyond the former fixed table capacity."""
    pack = root / 'normal-catalog'
    pack.mkdir()
    (pack / 'normal.png').write_bytes(FIXTURE.mod.encode_png(2, 2, bytes((128, 128, 128, 255)) * 4))
    lines = ['MINEWAYS_MOD_PACK\t3', 'texture\t0\tfixture:normals\tnormal.png', 'geometry\t0']
    expected = []
    for i in range(800):
        z = 1 - 2 * (i + .5) / 800
        angle = i * 2.399963229728653
        radius = math.sqrt(1-z*z)
        expected.append((radius*math.cos(angle), radius*math.sin(angle), z))
        a = (-math.sin(angle), math.cos(angle), 0)
        b = (-z*math.cos(angle), -z*math.sin(angle), radius)
        fields = ['polygon', '0', '3', '8', '8', '8', '0', '0']
        for vertex in (a, b):
            fields.extend(format(8+v*4, '.12g') for v in vertex)
            fields.extend(('16', '16'))
        lines.append('\t'.join(fields))
    lines.extend(('endgeometry', 'state\tfixture:many_normals\t808080\t255',
                  'usemodel\t0\t0\t0\t0', 'endstate'))
    manifest = pack / 'manifest.mwpack'
    manifest.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    source = pack / 'input.schem'
    schematic(source, ['fixture:many_normals'])
    assert sum(line.startswith('polygon\t') for line in lines) == 800
    if executable is None:
        return
    outputs = [root / ('normal-capacity-' + str(i) + '.obj') for i in range(2)]
    commands = ['Mod pack: ' + str(manifest), 'Minecraft world: ' + str(source),
                'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
                'Export lesser blocks: YES', 'Individual blocks: YES',
                'File type: Export individual textures to directory normaltex',
                'Selection location min to max: 0, 0, 0 to 0, 0, 0']
    details = run(executable, root, 'normal_capacity', [*commands,
                  *['Export for Rendering: ' + str(path) for path in outputs]])
    for output in outputs:
        check_obj(output, details, minimum_faces=800)
        normals, faces = [], []
        for line in output.read_text().splitlines():
            if line.startswith('vn '):
                normals.append(tuple(map(float, line.split()[1:])))
            elif line.startswith('f '):
                faces.append(line.split()[1:])
        assert len(normals) > 456 and len(faces) == 800, 'Mesh normal table did not grow'
        for face, target in zip(faces, expected):
            assert len(face) == 3
            indices = {int(corner.split('/')[2]) for corner in face}
            assert len(indices) == 1
            index = indices.pop()
            assert 1 <= index <= len(normals)
            assert sum(a*b for a, b in zip(normals[index-1], target)) > .999, 'Incorrect mesh normal'
    assert obj_geometry(outputs[0]) == obj_geometry(outputs[1]), 'Repeated mesh exports differ'


def exercise_precision(executable, root):
    """Preserve a real thin IE fragment across a 4,096-block selection."""
    pack = root / 'precision-pack'
    pack.mkdir()
    (pack / 'texture.png').write_bytes(FIXTURE.mod.encode_png(2, 2, bytes((80, 140, 200, 255)) * 4))
    vertices = [(4, -.000048, -.000032, 10.75, 10.5),
                (4.000048, 15.999984, -.000192, 10.75, 8.5),
                (4.000047995392386, 15.999984, 0, 10.749976002015831, 8.5)]
    manifest = pack / 'manifest.mwpack'
    manifest.write_text('\n'.join(['MINEWAYS_MOD_PACK\t3',
        'texture\t0\tprecision:pattern\ttexture.png', 'geometry\t0',
        'polygon\t0\t3\t' + '\t'.join(str(value) for vertex in vertices for value in vertex),
        'endgeometry', 'state\tprecision:thin\t808080\t255', 'usemodel\t0\t0\t90\t0', 'endstate', '']))
    state = 'precision:thin[retained=true]'
    wide, small = pack / 'wide.schem', pack / 'small.schem'
    schematic(wide, ['minecraft:air', state], [1] + [0] * 4095 + [1], (4097, 1, 1))
    schematic(small, [state])
    far_world = pack / 'far-world'
    world(far_world)
    far_x = 20000001
    region(far_world / 'region', 'precision:thin', chunk_x=far_x // 16)

    def commands(source, kind, output, mosaic=False):
        selection = (f'{far_x}, 0, 0 to {far_x+1}, 0, 0' if source == far_world else
                     '0, 0, 0 to ' + ('4096' if source == wide else '0') + ', 0, 0')
        return ['Mod pack: ' + str(manifest), 'Minecraft world: ' + str(source),
                'Set render type: ' + kind, 'Center model: NO', 'Individual blocks: NO',
                'Export lesser blocks: YES', 'Units for the model vertex data itself: meters',
                'Make Z the up direction instead of Y: NO',
                'Scale model by making each block 1000 mm high',
                'File type: ' + ('Export full color texture patterns' if mosaic else
                                'Export individual textures to directory precisiontex'),
                'Selection location min to max: ' + selection,
                'Export for Rendering: ' + str(output)]

    if executable is None:
        write_script(root, 'precision-fixture', commands(wide, 'Wavefront OBJ absolute indices', pack / 'wide.obj'))
        return
    f32 = lambda value: struct.unpack('f', struct.pack('f', value))[0]
    points = [tuple(f32(v) for v in vertex[:3]) for vertex in vertices]
    expected = [(-(p[2]-points[0][2])/16, (p[1]-points[0][1])/16, (p[0]-points[0][0])/16)
                for p in points[1:]]
    reference = None
    for mosaic in (False, True):
        label = 'precision-' + ('mosaic' if mosaic else 'individual')
        output = pack / (label + '.obj')
        details = run(executable, root, label, commands(wide, 'Wavefront OBJ absolute indices', output, mosaic))
        check_obj(output, details, minimum_faces=2)
        positions, faces = [], []
        for line in output.read_text().splitlines():
            fields = line.split()
            if fields and fields[0] == 'v': positions.append(tuple(map(float, fields[1:4])))
            elif fields and fields[0] == 'f': faces.append([positions[int(v.split('/')[0])-1] for v in fields[1:]])
        assert len(faces) == 2 and all(len(face) == 3 for face in faces)
        for face in faces:
            for point, edge in zip(face[1:], expected):
                assert all(abs(point[i]-face[0][i]-edge[i]) < 1e-11 for i in range(3)), 'Thin face lost position precision'
        reference = sorted(point for face in faces for point in face)
    output = pack / 'precision-ascii.stl'
    run(executable, root, 'precision-ascii', commands(wide, 'ASCII STL', output))
    stl = sorted(tuple(map(float, line.split()[1:4])) for line in output.read_text().splitlines()
                 if line.strip().startswith('vertex '))
    assert stl == reference, 'ASCII STL lost thin geometry or coordinate precision'
    for label, kind, suffix in [('usd', 'USD 1.0', '.usda'), ('vrml', 'VRML 2.0', '.wrl'),
                                ('magics', 'Binary STL iMaterialise', '.stl'), ('viscam', 'Binary STL VisCAM', '.stl')]:
        output = pack / (label + suffix)
        sentinel = b'existing output must survive\n'
        output.write_bytes(sentinel)
        details = run(executable, root, 'precision-' + label, commands(wide, kind, output, label == 'vrml'), expect_success=False)
        assert 'cannot preserve a thin face' in details, details
        assert output.read_bytes() == sentinel, 'Precision preflight overwrote the destination'
        run(executable, root, 'precision-local-' + label, commands(small, kind, output, label == 'vrml'))
        assert output.read_bytes() != sentinel and output.stat().st_size > 100
        if label in ('magics', 'viscam'):
            assert output.stat().st_size == 134, 'Binary STL triangle record size changed'
    # Adjacent starts cover both final vertex precision and odd world-to-box offsets.
    for far_x in (20000001, 20000002):
        far_obj = pack / 'far.obj'
        run(executable, root, 'precision-far-obj-' + str(far_x), commands(far_world, 'Wavefront OBJ absolute indices', far_obj))
        positions = [tuple(map(float, line.split()[1:4])) for line in far_obj.read_text().splitlines() if line.startswith('v ')]
        assert min(p[0] for p in positions) == far_x + 1, 'Uncentered world placement rounded an odd coordinate'
        far_stl = pack / 'far.stl'
        run(executable, root, 'precision-far-ascii-' + str(far_x), commands(far_world, 'ASCII STL', far_stl))
        stl = [tuple(map(float, line.split()[1:4])) for line in far_stl.read_text().splitlines()
               if line.strip().startswith('vertex ')]
        assert sorted(stl) == sorted(positions), 'ASCII STL lost absolute world placement'
    output = pack / 'far-instance.usda'
    output.write_bytes(sentinel)
    instance_commands = commands(far_world, 'USD 1.0', output)
    instance_commands.insert(-1, 'Individual blocks: YES')
    details = run(executable, root, 'precision-far-instance', instance_commands, expect_success=False)
    assert 'instance positions cannot preserve' in details and output.read_bytes() == sentinel, details
    print('PASS: wide OBJ/ASCII STL precision and explicit float-format preflight', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', nargs='?')
    parser.add_argument('--fixtures-only', action='store_true')
    parser.add_argument('--pack', type=Path, help='also test this externally compiled manifest (or pack directory)')
    parser.add_argument('--output', type=Path, help='retain fixtures, logs, and exports in a new directory')
    args = parser.parse_args()
    if not args.fixtures_only and not args.executable:
        parser.error('provide a Mineways executable or --fixtures-only')
    executable = Path(args.executable).resolve() if not args.fixtures_only else None
    if executable and not executable.is_file():
        parser.error('Mineways executable does not exist: ' + str(executable))
    manifest = args.pack.resolve() if args.pack else None
    if manifest and manifest.is_dir():
        manifest /= 'manifest.mwpack'
    if manifest and not manifest.is_file():
        parser.error('compiled manifest does not exist: ' + str(manifest))
    def execute(root):
        # Preserve the exact ordering regression even when no real pack is supplied.
        try:
            check_script_world_order(['Selection location min to max: 0, 0, 0 to 0, 0, 0',
                                      'Minecraft world: fixture.schem'])
        except AssertionError:
            pass
        else:
            raise AssertionError('Script-order check accepted a selection before world loading')
        exercise(executable, root)
        exercise_limits(executable, root)
        exercise_normals(executable, root)
        exercise_precision(executable, root)
        if manifest:
            exercise_pack(executable, root, manifest)
        elif executable is None:
            exercise_pack(None, root, root / 'pack' / 'manifest.mwpack')
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        execute(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='mineways mod windows ') as temporary:
            execute(Path(temporary))
    print('PASS: ' + ('portable Windows export fixtures' if executable is None else
                     'native mod geometry, adapters, invisible states, textures, schematic round trips, dimensions, uint16 IDs, 4096 texture limits, and syntax checks'))


if __name__ == '__main__':
    main()
