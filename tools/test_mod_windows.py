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


def schematic(path, names=None, indices=None, dimensions=None):
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
    assert manifest_text.splitlines()[0] in ('MINEWAYS_MOD_PACK\t1', 'MINEWAYS_MOD_PACK\t2', 'MINEWAYS_MOD_PACK\t3', 'MINEWAYS_MOD_PACK\t4', 'MINEWAYS_MOD_PACK\t5')
    selectors = {line.split('\t')[1] for line in manifest_text.splitlines() if line.startswith('state\t')}
    assert {'ae2:matrix_frame', 'ae2:drive[facing=north,spin=0]', 'ae2:quartz_glass',
            'ae2:spatial_pylon', 'allthemodium:molten_allthemodium_block[level=0]',
            'allthemodium:molten_allthemodium_block[level=7]'} <= selectors, 'Adapter fixture is incomplete'
    assert '\ninvisible\n' in manifest_text, 'Invisible fixture marker is missing'
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
    by_block, textures = {}, {}
    selectors = 0
    with manifest.open(encoding='utf-8') as stream:
        for line in stream:
            fields = line.rstrip('\r\n').split('\t')
            if fields[0] == 'state':
                state = fields[1]
                block = state.split('[', 1)[0]
                by_block.setdefault(block, [state, state])[1] = state
                if block in rotation_blocks:
                    rotation_states.append(state)
                selectors += 1
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
               'silentgear:material_grader']
    targets += [name for name in blocks if name.startswith('allthemodium:molten_')]
    for block in targets:
        if block in by_block:
            for state in by_block[block]:
                if state not in selected:
                    selected.append(state)
    names = []
    for state in selected:
        if names:
            names.append('minecraft:air')
        names.append(state)
    source = root / 'actual-input.schem'
    # Activate every installed block name through the palette, then render a bounded sample.
    # Place the sample in the first row; all other voxels are air. No live world is opened.
    palette = list(dict.fromkeys([by_block[block][0] for block in blocks] + names))
    assert len(palette) <= 65536, 'Full-pack test exceeds the Sponge palette limit'
    palette_index = {name: index for index, name in enumerate(palette)}
    width = max(512, len(names))
    length = (max(len(palette), len(names)) + width - 1) // width
    indices = [palette_index[name] for name in names]
    indices += [palette_index['minecraft:air']] * (width * length - len(indices))
    schematic(source, palette, indices, (width, 1, length))
    (root / 'selected-states.txt').write_text('\n'.join(selected) + '\n', encoding='utf-8')
    print(f'External catalog: {len(blocks)} blocks, {selectors} selectors, {len(textures)} textures; {len(selected)} sampled states', flush=True)
    common = ['Mod pack: ' + str(manifest),
              'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
              'Export lesser blocks: YES', 'Individual blocks: YES',
              'File type: Export individual textures to directory actualtex']
    selection = f'Selection location min to max: 0, 0, 0 to {len(names) - 1}, 0, 0'
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
