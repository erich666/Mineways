#!/usr/bin/env python3
"""Test packaged launcher helpers with complete pinned public packs, not a live save.

The installed layouts and sample worlds are test fixtures. Minecraft and the launcher
applications are not executed. Downloaded third-party assets stay outside the repository.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import gzip
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import unittest
import zipfile
from urllib.parse import urlsplit

import download_mod_assets as downloader
import make_preview_demo as nbt
from test_atm10_showcase import geometry, pack_sources
from test_mod_windows import check_export_pixels, check_obj, verify_schematic
from test_preview_package import app_command, app_environment

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = sorted((ROOT / 'tools/fixtures').glob('launcher-*.json'))


def fingerprint(root):
    return {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob('*')) if path.is_file()}


def fetch(entry, destination):
    downloader.check_url(entry['url'])
    return downloader.download_file(entry, destination)


def acquire(fixture, cache):
    fetch(dict(fixture['mrpack'], path='original.mrpack'), cache)
    with zipfile.ZipFile(cache / 'original.mrpack') as archive:
        index = json.loads(archive.read('modrinth.index.json'))
    files = index['files']
    if (index.get('formatVersion') != 1 or index.get('game') != 'minecraft'
            or index['dependencies'] != fixture['dependencies']
            or len(files) != fixture['expected_file_count']
            or sum(entry['path'].startswith('mods/') for entry in files) != fixture['expected_mod_count']
            or sum(entry['path'].startswith('mods/') and entry['path'].endswith('.jar')
                   for entry in files) != fixture['expected_active_mod_count']):
        raise AssertionError('Pinned public pack has unexpected metadata')
    seen = set()
    for entry in files:
        path = PurePosixPath(entry['path'])
        if (path.is_absolute() or '..' in path.parts or '\\' in entry['path'] or ':' in entry['path']
                or path.parts[0] not in ('mods', 'resourcepacks', 'shaderpacks')
                or not entry['path'].lower().endswith(('.jar', '.jar.disabled', '.zip')) or entry['path'].casefold() in seen):
            raise ValueError('Unsafe or duplicate public pack file path')
        seen.add(entry['path'].casefold())
        if (not re.fullmatch('[0-9a-f]{40}', entry['hashes']['sha1'])
                or type(entry['fileSize']) is not int or not 0 < entry['fileSize'] <= downloader.MAX_FILE):
            raise ValueError('Invalid public pack file size or digest')
        if urlsplit(entry['downloads'][0]).hostname != 'cdn.modrinth.com':
            raise ValueError('Unexpected public pack download host')
    fixture['index'] = index
    entries = [{'path': entry['path'], 'url': entry['downloads'][0], 'size': entry['fileSize'],
                'sha1': entry['hashes']['sha1']} for entry in files]
    entries += [dict(fixture['minecraft_client'], path='client.jar'),
                {key: fixture['loader'][key] for key in ('url', 'size', 'sha1')} | {'path': 'loader.jar'}]
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda entry: fetch(entry, cache), entries))
    print(f'Verified full pack: {fixture["name"]} {fixture["version"]}, '
          f'{fixture["expected_mod_count"]} mod files ({fixture["expected_active_mod_count"]} enabled)', flush=True)


def make_world(world, fixture):
    world.mkdir(parents=True)
    states = [sample['state'] for sample in fixture['samples']]
    palette = ['minecraft:air', 'minecraft:stone', *states]
    cells = [0] * 4096
    for z in range(16):
        for x in range(16):
            cells[x + z * 16] = 1
    for index, state in enumerate(states):
        cells[2 + index * 4 + 2 * 16 + 256] = index + 2
    nbt.write_region(world / 'region/r.0.0.mca', palette, cells)
    data = nbt.integer('version', 19133) + nbt.integer('DataVersion', 3953)
    data += nbt.compound('Version', nbt.integer('Id', 3953) + nbt.tag(8, 'Name', nbt.text('1.21.1')))
    data += nbt.tag(8, 'LevelName', nbt.text('Original ' + fixture['name'] + ' export test'))
    data += nbt.integer('SpawnX', 4) + nbt.integer('SpawnY', 66) + nbt.integer('SpawnZ', 2)
    (world / 'level.dat').write_bytes(gzip.compress(nbt.compound('', nbt.compound('Data', data)), mtime=0))
    (world / 'save-must-remain-unchanged.txt').write_text('Original test save. Do not modify.\n')


def install(fixture, cache, output):
    minecraft = fixture['index']['dependencies']['minecraft']
    loader = fixture['loader']
    launcher = output / 'launcher data with spaces'
    instance = launcher / ('instances' if fixture['layout'] == 'prism' else 'Instances') / fixture['name']
    game = instance / '.minecraft' if fixture['layout'] == 'prism' else instance
    game.mkdir(parents=True)
    for entry in fixture['index']['files']:
        path = Path(entry['path'])
        destination = game / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(cache / path, destination)
    with zipfile.ZipFile(cache / 'original.mrpack') as archive:
        for item in archive.infolist():
            path = PurePosixPath(item.filename)
            if path.parts[0] not in ('overrides', 'client-overrides') or item.is_dir():
                continue
            if '..' in path.parts or path.is_absolute() or '\\' in item.filename:
                raise ValueError('Unsafe pack override')
            target = game.joinpath(*path.parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(item))
    if fixture['layout'] == 'prism':
        components = [{'uid': 'net.minecraft', 'version': minecraft},
                      {'uid': 'net.fabricmc.fabric-loader', 'version': loader['version']}]
        (instance / 'mmc-pack.json').write_text(json.dumps({'formatVersion': 1, 'components': components}))
        (instance / 'instance.cfg').write_text('[General]\nInstanceType=OneSix\nname=' + fixture['name'] + '\n')
        client = launcher / f'libraries/com/mojang/minecraft/{minecraft}/minecraft-{minecraft}-client.jar'
        library = launcher / f'libraries/net/fabricmc/fabric-loader/{loader["version"]}/fabric-loader-{loader["version"]}.jar'
    else:
        version = loader['version']
        metadata = {'name': fixture['name'], 'gameVersion': minecraft,
                    'baseModLoader': {'name': 'neoforge-' + version, 'forgeVersion': version,
                                      'mavenVersionString': 'net.neoforged:neoforge:' + version,
                                      'minecraftVersion': minecraft}}
        (instance / 'minecraftinstance.json').write_text(json.dumps(metadata))
        client = launcher / f'Install/versions/{minecraft}/{minecraft}.jar'
        library = launcher / f'Install/libraries/net/neoforged/neoforge/{version}/neoforge-{version}-universal.jar'
    for source, destination in ((cache / 'client.jar', client), (cache / 'loader.jar', library)):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    world = game / 'saves/Original exporter test'
    make_world(world, fixture)
    return instance, world


def invoke_helper(package, instance, world, result, offline=False):
    if os.name == 'nt':
        powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        command = [str(powershell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                   str(package / 'tools/prepare_modpack_windows.ps1')]
    else:
        bundled = package / 'Mineways.app/Contents/Resources/tools/prepare_modpack.command'
        command = [str(bundled if bundled.is_file() else package / 'tools/prepare_modpack.command')]
    env = app_environment()
    if offline:
        env.update(HTTPS_PROXY='http://127.0.0.1:9', HTTP_PROXY='http://127.0.0.1:9',
                   https_proxy='http://127.0.0.1:9', http_proxy='http://127.0.0.1:9', NO_PROXY='')
    run = subprocess.run([*command, str(instance), str(world), str(result)], cwd=package,
                         env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                         errors='replace', timeout=1200)
    result.with_suffix('.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    if run.returncode:
        raise AssertionError('Packaged helper failed:\n' + run.stdout + run.stderr)
    value = result.read_text(encoding='utf-8-sig').strip()
    if value == 'VANILLA':
        raise AssertionError('Content pack was incorrectly classified as vanilla')
    manifest = Path(value)
    if not manifest.is_absolute() or not manifest.is_file():
        raise AssertionError('Helper did not return an existing absolute manifest path')
    return manifest


def check_sample_materials(obj, required):
    materials, current = {}, None
    for line in obj.with_suffix('.mtl').read_text(errors='replace').splitlines():
        if line.startswith('newmtl '):
            current = line[7:]
        elif line.startswith('map_Kd '):
            stem = Path(line[7:].replace('\\', '/')).stem
            if stem.startswith('mw_mod_'):
                materials[current] = int(stem[7:])
    vertices, seen = [], set()
    for line in obj.read_text(errors='replace').splitlines():
        if line.startswith('v '):
            vertices.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith('usemtl '):
            current = line[7:]
        elif line.startswith('f ') and current in materials:
            points = [vertices[int(corner.split('/')[0]) - 1] for corner in line.split()[1:]]
            for index, texture in enumerate(required):
                lower = (2 + index * 4, 65, 2)
                if materials[current] == texture and all(
                        lower[axis] - 1e-5 <= point[axis] <= lower[axis] + 1 + 1e-5
                        for point in points for axis in range(3)):
                    seen.add(index)
    if seen != set(range(len(required))):
        raise AssertionError('Sample positions do not use their expected model textures')


def validate(package, fixture, instance, world, output):
    saves_before = fingerprint(world.parent)
    manifest = invoke_helper(package, instance, world, output / 'prepared.txt')
    before = (manifest.stat().st_mtime_ns, hashlib.sha256(manifest.read_bytes()).hexdigest())
    cached = invoke_helper(package, instance, world, output / 'cached.txt', offline=True)
    if cached != manifest or before != (cached.stat().st_mtime_ns, hashlib.sha256(cached.read_bytes()).hexdigest()):
        raise AssertionError('Offline preparation rebuilt or changed the cached manifest')
    states = [sample['state'] for sample in fixture['samples']]
    textures = pack_sources(manifest, states)
    names = {}
    for line in manifest.open(encoding='utf-8'):
        fields = line.rstrip('\r\n').split('\t')
        if fields[0] == 'texture':
            names[fields[2]] = int(fields[1])
    required = [names[sample['texture']] for sample in fixture['samples']]
    executable = package / ('Mineways.exe' if os.name == 'nt' else 'Mineways.app/Contents/MacOS/mineways')
    sponge = output / 'roundtrip.schem'

    def export(label, source, bounds, mosaic=False):
        obj = output / (label + '.obj')
        commands = ['Mod pack: ' + str(manifest), 'Minecraft world: ' + str(source),
                    'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
                    'Export lesser blocks: YES', 'Individual blocks: YES',
                    'File type: ' + ('Export full color texture patterns' if mosaic else
                                     'Export individual textures to directory ' + label + '-textures'),
                    'Selection location min to max: ' + bounds, 'Export for Rendering: ' + str(obj)]
        if label == 'individual':
            commands.append('Export Schematic: ' + str(sponge))
        script = output / (label + '.mwscript')
        script.write_text('\n'.join([*commands, 'Close', '']), encoding='utf-8')
        run = subprocess.run(app_command(executable, script), cwd=package, env=app_environment(),
                             stdin=subprocess.DEVNULL, capture_output=True, text=True,
                             errors='replace', timeout=600)
        details = run.stdout + run.stderr
        (output / (label + '.log')).write_text(details, encoding='utf-8')
        if run.returncode or 'unknown block' in details.lower():
            raise AssertionError('Real pack export failed:\n' + details)
        check_obj(obj, details)
        return obj

    individual = export('individual', world, '0, 64, 0 to 15, 67, 15')
    checked = check_export_pixels(individual, textures)
    exported = {int(path.stem[7:]) for path in (output / 'individual-textures').glob('mw_mod_*.png')}
    if not set(required) <= exported:
        raise AssertionError('Distinctive sample model textures were not exported')
    check_sample_materials(individual, required)
    verify_schematic(sponge, states, '')
    mosaic = export('mosaic', world, '0, 64, 0 to 15, 67, 15', mosaic=True)
    restored = export('restored', sponge, '0, 0, 0 to 15, 3, 15')
    if geometry(individual) != geometry(mosaic) or geometry(individual) != geometry(restored):
        raise AssertionError('Real pack geometry differs between texture modes or Sponge round trip')
    if saves_before != fingerprint(world.parent):
        raise AssertionError('Launcher preparation or export modified the instance saves')
    metadata = json.loads((package / 'BUILD-METADATA.json').read_text(encoding='utf-8'))
    return {'source_revision': metadata['source_revision'], 'modrinth_version_id': fixture['modrinth_version_id'],
            'pack': fixture['name'], 'enabled_mods': fixture['expected_active_mod_count'], 'version': fixture['version'], 'layout': fixture['layout'],
            'minecraft': fixture['index']['dependencies']['minecraft'], 'loader': fixture['loader']['kind'],
            'loader_version': fixture['loader']['version'], 'sample_states': states,
            'source_pixel_textures': checked, 'faces': len(geometry(individual)),
            'offline_cache_reused': True, 'saves_unchanged': True, 'manifest': str(manifest)}


class FixtureChecks(unittest.TestCase):
    def test_complete_pinned_mod_lists(self):
        self.assertEqual(len(FIXTURES), 2)
        for path in FIXTURES:
            fixture = json.loads(path.read_text())
            self.assertGreater(fixture['expected_mod_count'], 50)
            self.assertGreaterEqual(fixture['expected_file_count'], fixture['expected_mod_count'])
            self.assertLessEqual(fixture['expected_active_mod_count'], fixture['expected_mod_count'])
            self.assertEqual(fixture['dependencies']['minecraft'], '1.21.1')
            for pin in (fixture['mrpack'], fixture['minecraft_client'], fixture['loader']):
                self.assertRegex(pin['sha1'], r'^[0-9a-f]{40}$')
                self.assertGreater(pin['size'], 0)
                downloader.check_url(pin['url'])
            self.assertEqual(urlsplit(fixture['mrpack']['url']).hostname, 'cdn.modrinth.com')



def validate_gui_exports(installations, gui_output):
    results = []
    references = json.loads((installations / 'validation.json').read_text(encoding='utf-8'))
    for setup_path in sorted(installations.glob('*/installation.json')):
        setup = json.loads(setup_path.read_text(encoding='utf-8'))
        fixture = json.loads(Path(setup['fixture']).read_text(encoding='utf-8'))
        reference = next(item for item in references if item['pack'] == fixture['name'])
        destination = gui_output / setup_path.parent.name
        obj = destination / 'individual.obj'
        states = [sample['state'] for sample in fixture['samples']]
        manifest = Path(reference['manifest'])
        textures = pack_sources(manifest, states)
        names = {}
        for line in manifest.read_text(encoding='utf-8').splitlines():
            fields = line.split('\t')
            if fields[0] == 'texture':
                names[fields[2]] = int(fields[1])
        check_obj(obj, '')
        pixels = check_export_pixels(obj, textures)
        check_sample_materials(obj, [names[sample['texture']] for sample in fixture['samples']])
        verify_schematic(destination / 'roundtrip.schem', states, '')
        if geometry(obj) != geometry(setup_path.parent / 'individual.obj'):
            raise AssertionError('Native picker current-scene geometry differs from the verified pack export')
        results.append({'pack': fixture['name'], 'sample_states': states,
                        'source_pixel_textures': pixels, 'faces': len(geometry(obj)),
                        'same_process_native_picker': True})
    if len(results) != 2:
        raise AssertionError('Expected both native launcher picker exports')
    (gui_output / 'validation.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print('PASS: native picker current-scene geometry, sample textures, and Sponge identity: ' + json.dumps(results))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path, nargs='?')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--assets-cache', type=Path)
    parser.add_argument('--fixture', type=Path, action='append')
    parser.add_argument('--assets-only', action='store_true', help='prepare original test installations without running the package')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--gui-output', type=Path, help='verify existing native picker exports against --output installations')
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[__file__])
        return
    if args.output is None or (args.package is None and not args.assets_only):
        parser.error('PACKAGE_ROOT and --output are required unless --assets-only')
    output = args.output.resolve()
    if args.gui_output:
        validate_gui_exports(output, args.gui_output.resolve())
        return
    output.mkdir(parents=True, exist_ok=False)
    cache = (args.assets_cache or output / 'asset cache').resolve()
    results = []
    for path in args.fixture or FIXTURES:
        fixture = json.loads(path.read_text(encoding='utf-8'))
        destination = output / path.stem
        destination.mkdir()
        assets = cache / path.stem
        acquire(fixture, assets)
        instance, world = install(fixture, assets, destination)
        setup = {'fixture': str(path.resolve()), 'instance': str(instance), 'world': str(world)}
        (destination / 'installation.json').write_text(json.dumps(setup, indent=2) + '\n')
        if not args.assets_only:
            result = validate(args.package.resolve(), fixture, instance, world, destination)
            results.append(result)
            print('PASS: ' + json.dumps(result), flush=True)
    (output / 'validation.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
