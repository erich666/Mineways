#!/usr/bin/env python3
"""Prepare models for a world in an installed CurseForge or Prism instance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import zipfile

import import_mod_assets as importer
from prepare_atm10_showcase import digest, inventory


def bounded_text(path, limit=16 * 1024 * 1024):
    with path.open('rb') as stream:
        value = stream.read(limit + 1)
    if len(value) > limit:
        raise ValueError(f'{path.name} exceeds the launcher metadata size limit.')
    return value.decode('utf-8-sig')


def metadata(path):
    data = json.loads(bounded_text(path))
    if not isinstance(data, dict):
        raise ValueError(f'{path.name} must contain a JSON object.')
    return data


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,127}', value):
        raise ValueError('The launcher metadata contains a missing or invalid game or loader version.')
    return value


def instance_info(selected):
    selected = selected.resolve(strict=True)
    root = selected
    if selected.name in ('minecraft', '.minecraft') and (selected.parent / 'mmc-pack.json').is_file():
        root = selected.parent
    if (root / 'mmc-pack.json').is_file():
        components = metadata(root / 'mmc-pack.json').get('components')
        if not isinstance(components, list) or not all(isinstance(c, dict) for c in components):
            raise ValueError('Prism instance metadata has no valid components list.')
        versions = {c.get('uid'): c.get('version') for c in components}
        mc = version(versions.get('net.minecraft'))
        loader = next(((kind, version(versions[uid])) for uid, kind in (
            ('net.neoforged', 'neoforge'), ('net.minecraftforge', 'forge'),
            ('net.fabricmc.fabric-loader', 'fabric')) if uid in versions), ('', ''))
        dirs = [root / name for name in ('minecraft', '.minecraft') if (root / name / 'saves').is_dir()]
        if selected != root:
            dirs = [selected]
        if len(dirs) != 1:
            raise ValueError('Select the Prism game folder that contains your saves.')
        game = dirs[0]
    else:
        game = root
        if (root / 'minecraftinstance.json').is_file():
            data = metadata(root / 'minecraftinstance.json')
            base = data.get('baseModLoader') or {}
            if not isinstance(base, dict):
                raise ValueError('Invalid CurseForge loader metadata.')
            mc = version(data.get('gameVersion') or base.get('minecraftVersion'))
            loader_id = base.get('name', '')
        elif (root / 'manifest.json').is_file() and (root / 'mods').is_dir():
            spec = metadata(root / 'manifest.json').get('minecraft', {})
            if not isinstance(spec, dict):
                raise ValueError('Invalid CurseForge Minecraft metadata.')
            mc = version(spec.get('version'))
            entries = spec.get('modLoaders', [])
            if not isinstance(entries, list) or not all(isinstance(e, dict) for e in entries):
                raise ValueError('Invalid CurseForge loader list.')
            loader_id = next((e.get('id', '') for e in entries if e.get('primary')), entries[0].get('id', '') if entries else '')
        else:
            raise ValueError('Choose an installed CurseForge instance or a Prism instance with mmc-pack.json.')
        name, _, number = str(loader_id).lower().partition('-')
        loader = (name, version(number)) if name in ('neoforge', 'forge', 'fabric') else ('', '')
    if game.resolve() != game or not (game / 'saves').is_dir():
        raise ValueError('The selected instance must contain a local saves folder. Open a world in the launcher first.')
    return root, game, mc, loader


def launcher_sources(selected, world):
    root, game, mc, (kind, number) = instance_info(selected)
    if world.name == 'level.dat':
        world = world.parent
    # Normalize /tmp and other parent aliases, but keep links inside the instance out.
    saves = game / 'saves'
    if saves.resolve() != saves or world.is_symlink() or world.resolve(strict=True).parent != saves:
        raise ValueError('Choose a world directly inside this instance’s saves folder.')
    level = world / 'level.dat'
    if not level.is_file() or level.resolve() != world.resolve() / 'level.dat':
        raise ValueError('The selected world needs a local level.dat file.')
    shared = root.parent.parent
    appdata = Path(os.environ.get('APPDATA', Path.home() / 'Library/Application Support'))
    libraries = [shared, shared / 'Install', game, appdata / 'PrismLauncher',
                 appdata / ('.minecraft' if os.name == 'nt' else 'minecraft')]
    clients = [p / rel for p in libraries for rel in (
        f'libraries/com/mojang/minecraft/{mc}/minecraft-{mc}-client.jar', f'versions/{mc}/{mc}.jar')]
    client = next((p for p in clients if p.is_file()), None)
    if client is None:
        raise ValueError(f'Minecraft {mc} client assets are missing. Start this pack in its launcher once and try again.')
    sources = [client]
    if kind in ('neoforge', 'forge'):
        coordinate = number if kind == 'neoforge' or number.startswith(mc + '-') else f'{mc}-{number}'
        group = 'net/neoforged/neoforge' if kind == 'neoforge' else 'net/minecraftforge/forge'
        jars = (p / f'libraries/{group}/{coordinate}/{kind}-{coordinate}{suffix}.jar'
                for p in libraries for suffix in ('-universal', ''))
        jar = next((p for p in jars if p.is_file()), None)
        if jar is None:
            raise ValueError(f'{kind} {number} assets are missing. Start this pack in its launcher once and try again.')
        sources.append(jar)
    elif kind == 'fabric':
        # Fabric's loader JAR is optional for model data. Some installs keep it locally.
        number = number.removesuffix('-' + mc)
        rel = f'libraries/net/fabricmc/fabric-loader/{number}/fabric-loader-{number}.jar'
        jar = next((p / rel for p in libraries if (p / rel).is_file()), None)
        if jar is not None:
            sources.append(jar)
    mods = game / 'mods'
    if mods.is_dir():
        if mods.resolve() != mods:
            raise ValueError('The mods folder must be inside the selected instance.')
        sources.extend(sorted((p for p in mods.iterdir() if p.suffix.lower() == '.jar'), key=lambda p: (p.name.casefold(), p.name)))
    options = game / 'options.txt'
    if options.is_file():
        line = next((s for s in bounded_text(options).splitlines() if s.startswith('resourcePacks:')), None)
        enabled = json.loads(line.partition(':')[2]) if line else []
        if not isinstance(enabled, list) or not all(isinstance(name, str) for name in enabled):
            raise ValueError('The enabled resource pack list in options.txt is invalid.')
        for entry in enabled:
            filename = entry.removeprefix('file/')
            if not filename or filename in ('.', '..') or any(c in filename for c in '/\\:\x00'):
                if entry.startswith('file/'):
                    raise ValueError('An enabled resource pack must be inside the resourcepacks folder.')
                continue
            item = game / 'resourcepacks' / filename
            if item.exists():
                if item.resolve() != item:
                    raise ValueError('The enabled resource pack must be inside this instance.')
                sources.append(item)
    if (world / 'resources.zip').is_file():
        sources.append(world / 'resources.zip')
    return sources


def fingerprint(sources):
    value = hashlib.sha256(b'Mineways launcher cache v1')
    total = count = 0
    if len(sources) > importer.MAX_SOURCES:
        raise ValueError('Too many asset sources.')
    def file_hash(name, path, limit):
        nonlocal total, count
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise ValueError(f'Invalid or oversized asset source: {path.name}')
        total += info.st_size
        count += 1
        if total > importer.MAX_SOURCE_BYTES or count > importer.MAX_FILES:
            raise ValueError('The installed pack exceeds the importer size limit.')
        value.update(name.encode('utf-8') + b'\0' + bytes.fromhex(digest(path)))
    tools = Path(__file__).resolve().parent
    for source in [Path(__file__).resolve(), tools / 'import_mod_assets.py', *sorted(tools.glob('mod_*.py')), *sources]:
        value.update(b'\0source\0')
        if source.is_symlink():
            raise ValueError(f'Symbolic link in asset sources: {source.name}')
        if not source.is_dir():
            file_hash(source.name, source, importer.MAX_ARCHIVE_BYTES)
            continue
        for directory, dirs, files in os.walk(source, followlinks=False):
            dirs.sort()
            for name in dirs + files:
                path = Path(directory, name)
                if path.resolve() != path:
                    raise ValueError(f'Symbolic link in resource pack: {name}')
            for name in sorted(files):
                path = Path(directory, name)
                relative = path.relative_to(source).as_posix()
                if importer.Assets.wanted(relative):
                    file_hash(relative, path, importer.MAX_FILE)
    return value.hexdigest()


def prepare(package, instance, world):
    sources = launcher_sources(instance, world)
    key = fingerprint(sources)
    cache = package / '.modpack-cache'
    if cache.resolve() != cache:
        raise ValueError('The Mineways cache must be inside the extracted preview.')
    cache.mkdir(exist_ok=True)
    target = cache / key

    def result():
        if target.is_symlink():
            raise ValueError('The Mineways cache folder contains a symbolic link.')
        receipt = target / 'prepared.json'
        if not receipt.is_file() or receipt.is_symlink() or metadata(receipt) != inventory(target, receipt.name):
            raise ValueError(f'The prepared pack files changed. Remove {target} and try again.')
        manifest = target / 'manifest.mwpack'
        if manifest.is_file() and not manifest.is_symlink() and (target / 'report.json').is_file():
            return str(manifest)
        if (target / 'vanilla').is_file():
            return 'VANILLA'
        raise ValueError(f'The prepared pack is incomplete. Remove {target} and try again.')

    if target.exists():
        return result()
    print(f'Preparing models and textures from {len(sources)} sources.', flush=True)
    with tempfile.TemporaryDirectory(prefix='.prepare-', dir=cache) as temporary:
        staged = Path(temporary) / 'pack'
        try:
            importer.compile_pack(sources, staged)
        except importer.NoModDefinitions:
            staged.mkdir()
            (staged / 'vanilla').touch()
        if fingerprint(launcher_sources(instance, world)) != key:
            raise ValueError('The pack changed during preparation. Choose the pack again.')
        (staged / 'prepared.json').write_text(json.dumps(inventory(staged, 'prepared.json')), encoding='utf-8')
        try:
            staged.rename(target)
        except OSError:
            if not target.is_dir():
                raise
    return result()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('instance', 'world', 'result'):
        parser.add_argument(arg, type=Path)
    args = parser.parse_args()
    try:
        value = prepare(Path(__file__).resolve().parents[1], args.instance, args.world)
        args.result.write_text(value, encoding='utf-8')
    except (ValueError, OSError, RuntimeError, zipfile.BadZipFile) as error:
        if os.name == 'nt':
            Path(str(args.result) + '.error.txt').write_text(str(error), encoding='utf-8')
        parser.exit(1, f'Modpack preparation failed: {error}\n')


if __name__ == '__main__':
    main()
