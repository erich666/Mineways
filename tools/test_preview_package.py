#!/usr/bin/env python3
"""Extract a preview into a path with spaces and test its bundled app and importer."""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile


def extract_package(archive_path, destination):
    with zipfile.ZipFile(archive_path) as archive:
        roots = set()
        for member in archive.infolist():
            relative = PurePosixPath(member.filename)
            if relative.is_absolute() or '..' in relative.parts or '\\' in member.filename or ':' in member.filename:
                raise ValueError('Unsafe archive path: ' + member.filename)
            roots.add(relative.parts[0])
            target = destination.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            if stat.S_ISLNK(member.external_attr >> 16):
                link = archive.read(member).decode('utf-8')
                if PurePosixPath(link).is_absolute() or '\\' in link or ':' in link:
                    raise ValueError('Unsafe archive symlink: ' + member.filename)
                resolved = (target.parent / link).resolve()
                if not resolved.is_relative_to(destination.resolve() / relative.parts[0]):
                    raise ValueError('Symlink escapes package: ' + member.filename)
                target.symlink_to(link)
            elif member.is_dir():
                target.mkdir(exist_ok=True)
            else:
                target.write_bytes(archive.read(member))
                mode = member.external_attr >> 16
                if mode:
                    target.chmod(stat.S_IMODE(mode))
    if len(roots) != 1:
        raise ValueError('Expected one package root')
    root = destination / roots.pop()
    expected = {}
    for line in (root / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        checksum, name = line.split('  ', 1)
        expected[name] = checksum
    actual = {}
    for path in root.rglob('*'):
        if (path.is_file() or path.is_symlink()) and path != root / 'SHA256SUMS':
            data = os.fsencode(os.readlink(path)) if path.is_symlink() else path.read_bytes()
            actual[path.relative_to(root).as_posix()] = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise AssertionError('Package checksum inventory does not match extracted files')
    return root


def app_environment():
    env = os.environ.copy()
    if os.name == 'nt':
        system = Path(env['SystemRoot'])
        env['PATH'] = str(system / 'System32') + os.pathsep + str(system)
    else:
        env['PATH'] = '/usr/bin:/bin:/usr/sbin:/sbin'
    for key in ('DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH', 'DYLD_INSERT_LIBRARIES'):
        env.pop(key, None)
    return env


def run(command, cwd, env=None):
    result = subprocess.run(command, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True,
                            text=True, errors='replace', timeout=120, env=env)
    if result.returncode:
        raise AssertionError(f'Command exited {result.returncode}: {command}\n{result.stdout}\n{result.stderr}')
    return result.stdout + result.stderr


def export(executable, root, name, world, selection, dimension=None, schematic=False, minimum_faces=6, mosaic=False):
    output = root / 'smoke output'
    output.mkdir(exist_ok=True)
    obj = output / (name + '.obj')
    schem = output / (name + '.schem')
    commands = ['Mod pack: ' + str(root / 'demo/manifest.mwpack'),
                'Minecraft world: ' + str(world)]
    if dimension:
        commands.append('Dimension: ' + dimension)
    commands += ['Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
                 'Export lesser blocks: YES', 'Individual blocks: YES',
                 'File type: ' + ('Export full color texture patterns' if mosaic else
                                  'Export individual textures to directory ' + name + 'tex'),
                 'Selection location min to max: ' + selection,
                 'Export for Rendering: ' + str(obj)]
    if schematic:
        commands.append('Export Schematic: ' + str(schem))
    commands.append('Close')
    script = output / (name + '.mwscript')
    script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    run([str(executable), '-headless', str(script)], root, env=app_environment())
    text = obj.read_text(encoding='utf-8', errors='replace')
    faces = [line for line in text.splitlines() if line.startswith('f ')]
    vertices = [tuple(map(float, line.split()[1:4])) for line in text.splitlines() if line.startswith('v ')]
    if len(faces) < minimum_faces or not vertices or not all(math.isfinite(c) for point in vertices for c in point):
        raise AssertionError('Export has missing or invalid geometry')
    for face in faces:
        if any(not 1 <= int(corner.split('/')[0]) <= len(vertices) for corner in face.split()[1:]):
            raise AssertionError('Export has invalid face indices')
    if dimension and '# Dimension: ' + dimension not in text:
        raise AssertionError('Export lost dimension identity')
    return obj, schem


PIXEL_CHECK = r'''
import importlib.util, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location('packaged_importer', root / 'tools/import_mod_assets.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
obj = pathlib.Path(sys.argv[2])
libraries = [obj.parent / line[7:].strip() for line in obj.read_text().splitlines() if line.startswith('mtllib ')]
assert libraries, 'OBJ has no material library'
used = {line[7:].strip() for line in obj.read_text().splitlines() if line.startswith('usemtl ')}
pixels = set()
textures = set()
for library in libraries:
    material = None
    for line in library.read_text().splitlines():
        if line.startswith('newmtl '):
            material = line[7:].strip()
        if line.startswith('map_Kd ') and material in used:
            path = library.parent / line[7:].strip().replace('\\', '/')
            textures.add(path)
for path in textures:
    width, height, rgba = module.decode_png(path.read_bytes())
    pixels.update(tuple(rgba[i:i+4]) for i in range(0, len(rgba), 4))
expected = json.loads((root / 'demo/expected.json').read_text())
for color in expected['expected_texture_colors']:
    assert tuple(color) in pixels, 'Original demo texture color is missing: ' + str(color)
print('Packaged material pixels verified')
'''


def check_mac_cli_failures(executable, root, selection):
    output = root / 'smoke output'
    output.mkdir(exist_ok=True)
    unknown = output / 'invalid-command.mwscript'
    unknown.write_text('Unknown preview command: YES\nClose\n', encoding='utf-8')
    failed_export = output / 'failed-export.mwscript'
    failed_export.write_text('\n'.join([
        'Mod pack: ' + str(root / 'demo/manifest.mwpack'),
        'Minecraft world: ' + str(root / 'demo/world'),
        'Selection location min to max: ' + selection,
        'Export for Rendering: ' + str(output / 'missing directory/model.obj'),
        'Close', '']), encoding='utf-8')
    for script in (output / 'missing-script.mwscript', unknown, failed_export):
        result = subprocess.run([str(executable), '-headless', str(script)], cwd=root,
                                stdin=subprocess.DEVNULL, capture_output=True,
                                text=True, errors='replace', timeout=20, env=app_environment())
        if result.returncode != 1 or not (result.stdout + result.stderr).strip():
            raise AssertionError(f'Headless failure did not return a diagnostic and exit 1: {script.name}\n'
                                 f'{result.returncode}\n{result.stdout}\n{result.stderr}')
    print('Packaged Mac CLI rejects missing scripts, unknown commands, and failed exports without dialogs')


def smoke(archive, destination):
    root = extract_package(archive, destination)
    metadata = json.loads((root / 'BUILD-METADATA.json').read_text(encoding='utf-8'))
    executable = root / ('Mineways.exe' if metadata['platform'] == 'windows-x64' else 'Mineways.app/Contents/MacOS/mineways')
    expected = json.loads((root / 'demo/expected.json').read_text(encoding='utf-8'))
    # -I excludes the checkout and PYTHONPATH, including for importer helper resolution.
    run([sys.executable, '-I', str(root / 'tools/import_mod_assets.py'),
         str(root / 'demo/source-assets'), '-o', str(root / 'recompiled demo')], root)
    report = json.loads((root / 'recompiled demo/report.json').read_text(encoding='utf-8'))
    if report['blocks'] < 1 or report['unsupported'] or report['unsupported_states']:
        raise AssertionError('Packaged importer could not compile the demo source assets')
    selection = ', '.join(map(str, expected['selection_min'])) + ' to ' + ', '.join(map(str, expected['selection_max']))
    if metadata['platform'].startswith('macos-'):
        check_mac_cli_failures(executable, root, selection)
    obj, schem = export(executable, root, 'world', root / 'demo/world', selection, schematic=True,
                        minimum_faces=expected['minimum_faces'])
    raw = schem.read_bytes()
    if raw.startswith(b'\x1f\x8b'):
        raw = gzip.decompress(raw)
    for state in expected['schematic_states']:
        if state.encode('utf-8') not in raw:
            raise AssertionError('Sponge export lost original state: ' + state)
    print(run([sys.executable, '-I', '-c', PIXEL_CHECK, str(root), str(obj)], root).strip())
    mosaic, _ = export(executable, root, 'mosaic', root / 'demo/world', selection,
                       minimum_faces=expected['minimum_faces'], mosaic=True)
    print(run([sys.executable, '-I', '-c', PIXEL_CHECK, str(root), str(mosaic)], root).strip())
    export(executable, root, 'dimension', root / 'demo/world', selection, dimension=expected['dimension'],
           minimum_faces=expected['minimum_faces'])
    schematic_selection = ', '.join(map(str, expected['schematic_selection_min'])) + ' to ' + ', '.join(map(str, expected['schematic_selection_max']))
    export(executable, root, 'schematic', root / 'demo/demo.schem', schematic_selection,
           minimum_faces=expected['minimum_faces'])
    print('Packaged app, importer, geometry, textures, Sponge identity, and custom dimension passed: ' + metadata['platform'])


class ArchiveTests(unittest.TestCase):
    def test_reject_path_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            archive = path / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('../escaped', 'bad')
            with self.assertRaises(ValueError):
                extract_package(archive, path / 'output')
            self.assertFalse((path / 'escaped').exists())

    def test_reject_unsafe_symlinks(self):
        for target in ('/tmp/outside', '../../outside', r'C:\outside', r'..\outside'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary)
                archive = path / 'bad.zip'
                with zipfile.ZipFile(archive, 'w') as stream:
                    info = zipfile.ZipInfo('preview/link')
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    stream.writestr(info, target)
                with self.assertRaises(ValueError):
                    extract_package(archive, path / 'output')

    @unittest.skipIf(os.name == 'nt', 'Windows package contains no symlinks')
    def test_preserve_bundle_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            archive = path / 'bundle.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('preview/real', b'app')
                info = zipfile.ZipInfo('preview/link')
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                stream.writestr(info, 'real')
                stream.writestr('preview/SHA256SUMS', hashlib.sha256(b'app').hexdigest() + '  real\n' +
                                hashlib.sha256(b'real').hexdigest() + '  link\n')
            root = extract_package(archive, path / 'output')
            self.assertTrue((root / 'link').is_symlink())
            self.assertEqual((root / 'link').read_bytes(), b'app')

    def test_permissions_and_checksum(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            archive = path / 'sample.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                info = zipfile.ZipInfo('preview/app')
                info.external_attr = (stat.S_IFREG | 0o755) << 16
                stream.writestr(info, b'app')
                stream.writestr('preview/SHA256SUMS', hashlib.sha256(b'app').hexdigest() + '  app\n')
            root = extract_package(archive, path / 'output')
            self.assertEqual((root / 'app').read_bytes(), b'app')
            if os.name != 'nt':
                self.assertTrue((root / 'app').stat().st_mode & stat.S_IXUSR)

    def test_reject_changed_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            archive = path / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('preview/app', b'changed')
                stream.writestr('preview/SHA256SUMS', hashlib.sha256(b'app').hexdigest() + '  app\n')
            with self.assertRaises(AssertionError):
                extract_package(archive, path / 'output')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', nargs='?', type=Path)
    parser.add_argument('--output', type=Path, help='new directory for retained test files')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[sys.argv[0]])
    if not args.archive:
        parser.error('provide an archive or --self-test')
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        destination = args.output / 'extracted preview with spaces'
        destination.mkdir()
        smoke(args.archive.resolve(), destination.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='Mineways preview with spaces ') as temporary:
            smoke(args.archive.resolve(), Path(temporary).resolve())


if __name__ == '__main__':
    main()
