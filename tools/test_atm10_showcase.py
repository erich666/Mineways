#!/usr/bin/env python3
"""Validate a shipped ATM10 showcase with its packaged app and locally compiled pack.

Usage: python tools/test_atm10_showcase.py PACKAGE_ROOT --output NEW_DIRECTORY
Run the package's setup entrypoint with --prepare-only first. No asset download occurs here.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_preview_package import app_command, app_environment
from test_mod_windows import check_export_pixels, check_obj, verify_schematic


def state_parts(state):
    name, _, properties = state.partition('[')
    return name, dict(item.split('=', 1) for item in properties.rstrip(']').split(',') if item)


def pack_sources(manifest, states):
    wanted = {state: state_parts(state) for state in states}
    supported, textures = set(), {}
    with manifest.open(encoding='utf-8') as stream:
        for line in stream:
            fields = line.rstrip('\r\n').split('\t')
            if fields[0] == 'state':
                name, properties = state_parts(fields[1])
                for state, (target, values) in wanted.items():
                    if target == name and all(values.get(key) == value for key, value in properties.items()):
                        supported.add(state)
            elif fields[0] == 'texture':
                texture = (manifest.parent / fields[3]).resolve()
                if not texture.is_relative_to(manifest.parent.resolve()):
                    raise AssertionError('Texture escapes pack root')
                textures[int(fields[1])] = texture
    missing = set(states) - supported
    if missing:
        raise AssertionError('Showcase states absent from compiled pack: ' + ', '.join(sorted(missing)))
    return textures


def geometry(path):
    """Compare face positions independently of material names and texture atlas UVs."""
    vertices, faces = [], []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('v '):
            vertex = tuple(map(float, line.split()[1:4]))
            if len(vertex) != 3 or not all(math.isfinite(value) for value in vertex):
                raise AssertionError('Invalid vertex in ' + str(path))
            vertices.append(vertex)
        elif line.startswith('f '):
            indices = [int(corner.split('/')[0]) for corner in line.split()[1:]]
            if len(indices) < 3 or any(not 1 <= index <= len(vertices) for index in indices):
                raise AssertionError('Invalid face indices in ' + str(path))
            faces.append(tuple(vertices[index - 1] for index in indices))
    if not faces:
        raise AssertionError('No geometry in ' + str(path))
    # Sponge restores the same selection at a local origin.
    origin = tuple(min(vertex[axis] for vertex in vertices) for axis in range(3))
    return sorted(tuple(tuple(round(vertex[axis] - origin[axis], 7) for axis in range(3))
                        for vertex in face) for face in faces)


def selection(lower, upper):
    return ', '.join(map(str, lower)) + ' to ' + ', '.join(map(str, upper))


def validate(package, manifest, output):
    metadata = json.loads((package / 'BUILD-METADATA.json').read_text(encoding='utf-8'))
    showcase = package / 'atm10-showcase'
    catalog = json.loads((showcase / 'catalog.json').read_text(encoding='utf-8'))
    if catalog['schema_version'] != 1 or len(catalog['showcase_blocks']) != catalog['block_count']:
        raise AssertionError('Invalid showcase catalog')
    states = [entry['state'] for entry in catalog['showcase_blocks']]
    textures = pack_sources(manifest, states)
    executable = package / ('Mineways.exe' if metadata['platform'] == 'windows-x64'
                            else 'Mineways.app/Contents/MacOS/mineways')
    output.mkdir(parents=True, exist_ok=False)
    bounds = selection(catalog['selection_min'], catalog['selection_max'])
    sponge = output / 'showcase.schem'

    def export(name, world, bounds, mosaic=False, schematic=False):
        obj = output / (name + '.obj')
        commands = ['Mod pack: ' + str(manifest), 'Minecraft world: ' + str(world),
                    'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
                    'Export lesser blocks: YES', 'Individual blocks: YES',
                    'File type: ' + ('Export full color texture patterns' if mosaic else
                                     'Export individual textures to directory ' + name + '-textures'),
                    'Selection location min to max: ' + bounds,
                    'Export for Rendering: ' + str(obj)]
        if schematic:
            commands.append('Export Schematic: ' + str(sponge))
        commands.append('Close')
        script = output / (name + '.mwscript')
        script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
        result = subprocess.run(app_command(executable, script), cwd=package, env=app_environment(),
                                stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                errors='replace', timeout=600)
        details = result.stdout + result.stderr
        (output / (name + '.log')).write_text(details, encoding='utf-8')
        if result.returncode or 'unknown block' in details.lower():
            raise AssertionError(f'{name}: export failed or has unknown blocks\n{details}')
        check_obj(obj, details, minimum_faces=len(states))
        geometry(obj)
        return obj

    individual = export('individual', showcase / catalog['world'], bounds, schematic=True)
    verify_schematic(sponge, states, '')
    checked = check_export_pixels(individual, textures)
    mosaic = export('mosaic', showcase / catalog['world'], bounds, mosaic=True)
    expected = geometry(individual)
    if expected != geometry(mosaic):
        raise AssertionError('Mosaic export changed showcase geometry')
    local_bounds = selection(catalog['schematic_selection_min'], catalog['schematic_selection_max'])
    shipped = export('shipped-schematic', showcase / catalog['schematic'], local_bounds)
    if expected != geometry(shipped):
        raise AssertionError('Shipped schematic differs from showcase world')
    restored = export('roundtrip', sponge, local_bounds)
    if expected != geometry(restored):
        raise AssertionError('Sponge round trip changed showcase geometry')
    result = {'source_revision': metadata['source_revision'], 'asset_snapshot': catalog['asset_snapshot'],
              'platform': metadata['platform'], 'showcase_blocks': len(states),
              'pixel_checked_textures': checked, 'faces': len(expected),
              'texture_modes_identical_geometry': True, 'sponge_identity_and_geometry': True, 'shipped_schematic_geometry': True}
    (output / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('PASS: packaged ATM10 showcase: ' + json.dumps(result), flush=True)


class Checks(unittest.TestCase):
    def test_geometry_ignores_uvs_and_origin_but_checks_indices(self):
        with tempfile.TemporaryDirectory() as temporary:
            first, second = Path(temporary) / 'a.obj', Path(temporary) / 'b.obj'
            first.write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1 2/2 3/3\n')
            second.write_text('v 4 5 6\nv 5 5 6\nv 4 6 6\nf 1/9 2/8 3/7\n')
            self.assertEqual(geometry(first), geometry(second))
            second.write_text('v 0 0 0\nf 1 2 3\n')
            with self.assertRaises(AssertionError):
                geometry(second)

    def test_pack_rejects_unsupported_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / 'manifest.mwpack'
            manifest.write_text('MINEWAYS_MOD_PACK\t6\nstate\tmod:block[facing=north]\tffffff\t255\n')
            self.assertEqual(pack_sources(manifest, ['mod:block[facing=north,powered=false]']), {})
            with self.assertRaises(AssertionError):
                pack_sources(manifest, ['mod:block[facing=south]'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, nargs='?')
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[__file__])
        return
    if args.package is None or args.output is None:
        parser.error('PACKAGE_ROOT and --output are required')
    package = args.package.resolve()
    manifest = (args.manifest or package / 'atm10-pack/manifest.mwpack').resolve()
    validate(package, manifest, args.output.resolve())


if __name__ == '__main__':
    main()
