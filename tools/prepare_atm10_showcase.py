#!/usr/bin/env python3
"""Prepare the bundled ATM10 showcase, then open it in the bundled application."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile


def helper(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def inventory(pack, receipt='showcase-setup.json'):
    files = {}
    for path in sorted(pack.rglob('*')):
        if path.is_symlink():
            raise ValueError('The generated showcase pack contains a symbolic link.')
        if path.is_file() and path != pack / receipt:
            files[path.relative_to(pack).as_posix()] = digest(path)
    return files


def signature(root):
    paths = [root / 'tools/fixtures/atm10-showcase-1.21.1.json',
             root / 'tools/import_mod_assets.py', *sorted((root / 'tools').glob('mod_*.py'))]
    value = hashlib.sha256()
    for path in paths:
        value.update(path.name.encode('utf-8') + b'\0' + bytes.fromhex(digest(path)))
    return value.hexdigest()


def prepare(root):
    pack = root / 'atm10-pack'
    stamp = pack / 'showcase-setup.json'
    expected = signature(root)
    if pack.is_symlink():
        raise ValueError('The generated showcase pack must not be a symbolic link.')
    if pack.exists():
        saved = json.loads(stamp.read_text(encoding='utf-8')) if stamp.is_file() else {}
        if (saved.get('signature') != expected or saved.get('files') != inventory(pack)
                or not (pack / 'manifest.mwpack').is_file()):
            raise ValueError('The generated ATM10 pack is incomplete or changed. '
                             'Extract a fresh preview folder and open its showcase launcher.')
        print('Prepared ATM10 assets verified. No download is needed.', flush=True)
        return pack
    fixture = root / 'tools/fixtures/atm10-showcase-1.21.1.json'
    downloader = helper('download_mod_assets')
    entries = downloader.read_manifest(fixture)
    megabytes = sum(entry['size'] for entry in entries) / 1000000
    print(f'First launch: downloading {len(entries)} verified asset files ({megabytes:.0f} MB).', flush=True)
    print('Files stay inside this preview folder. No Minecraft or system Python installation is needed.', flush=True)
    cache = root / '.showcase-assets'
    if cache.is_symlink():
        raise ValueError('The showcase asset cache must not be a symbolic link.')
    sources = downloader.download_manifest(fixture, cache)
    print('Preparing the showcase models and textures. This can take several minutes.', flush=True)
    importer = helper('import_mod_assets')
    # Publish only a complete pack with its verification inventory.
    with tempfile.TemporaryDirectory(prefix='.showcase-prepare-', dir=root) as temporary:
        staged = Path(temporary) / 'pack'
        importer.compile_pack(sources.read_text(encoding='utf-8').splitlines(), staged)
        saved = {'signature': expected, 'files': inventory(staged)}
        (staged / stamp.name).write_text(json.dumps(saved, sort_keys=True) + '\n', encoding='utf-8')
        staged.rename(pack)
    print('The showcase models and textures are ready.', flush=True)
    return pack


def write_launch_script(root, pack, windows=None):
    windows = os.name == 'nt' if windows is None else windows
    catalog = json.loads((root / 'atm10-showcase/catalog.json').read_text(encoding='utf-8'))
    low, high = catalog['selection_min'], catalog['selection_max']
    focus = catalog['focus']
    commands = (['Show informational: script', 'Show warning: script', 'Show error: script']
                if windows else [])
    commands += ['Mod pack: ' + str(pack / 'manifest.mwpack'),
                 'Minecraft world: ' + str(root / 'atm10-showcase/world'),
                 'Set render type: Wavefront OBJ absolute indices', 'Center model: NO',
                 'Export lesser blocks: YES', 'Individual blocks: YES',
                 'File type: Export individual textures to directory showcase-textures',
                 'Selection location min to max: ' + ', '.join(map(str, low)) + ' to ' + ', '.join(map(str, high)),
                 f'Focus view: {focus[0]}, {focus[2]}', 'Zoom: 16']
    script = root / 'atm10-showcase/open-showcase.mwscript'
    script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    return script


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true', help='prepare and verify without opening a window')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        pack = prepare(root)
        script = write_launch_script(root, pack)
        if not args.prepare_only:
            executable = root / ('Mineways.exe' if os.name == 'nt' else 'Mineways.app/Contents/MacOS/mineways')
            command = [str(executable), *(['-s', 'none'] if os.name == 'nt' else []), str(script)]
            print('Opening the ATM10 showcase. The gallery is already selected for export.', flush=True)
            subprocess.Popen(command, cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=os.name != 'nt')
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'ATM10 setup failed: {error}\nOpen ATM10 Showcase again to retry interrupted downloads.\n')


if __name__ == '__main__':
    main()
