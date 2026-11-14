#!/usr/bin/env python3
"""Create a portable preview ZIP from an already built executable or app bundle."""
import argparse
import hashlib
import json
import os
import re
import plistlib
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ('windows-x64', 'macos-x86_64', 'macos-arm64', 'macos-universal')
MAC_CPUS = {'arm64': 0x100000c, 'x86_64': 0x1000007}


def macho_cpus(path):
    with path.open('rb') as stream:
        header = stream.read(8)
        if header[:4] == b'\xcf\xfa\xed\xfe' and len(header) == 8:
            return {struct.unpack_from('<I', header, 4)[0]}
        if header[:4] in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf') and len(header) == 8:
            count = struct.unpack_from('>I', header, 4)[0]
            if not 1 <= count <= 16:
                raise ValueError('Invalid universal Mach-O architecture count')
            size = 32 if header[:4] == b'\xca\xfe\xba\xbf' else 20
            entries = stream.read(count * size)
            if len(entries) != count * size:
                raise ValueError('Truncated universal Mach-O header')
            return {struct.unpack_from('>I', entries, i * size)[0] for i in range(count)}
    return None


def digest(path):
    data = os.fsencode(os.readlink(path)) if path.is_symlink() else path.read_bytes()
    return hashlib.sha256(data).hexdigest()


def check_architecture(app, platform):
    binary = app if platform == 'windows-x64' else app / 'Contents/MacOS/mineways'
    with binary.open('rb') as stream:
        header = stream.read(64)
        if platform == 'windows-x64':
            if header[:2] != b'MZ' or len(header) < 64:
                raise ValueError('Expected a Windows PE executable')
            stream.seek(struct.unpack_from('<I', header, 60)[0])
            if stream.read(6) != b'PE\0\0\x64\x86':
                raise ValueError('Expected a Windows x64 executable')
    if platform.startswith('macos-'):
        expected = set(MAC_CPUS.values()) if platform == 'macos-universal' else {MAC_CPUS[platform[6:]]}
        if macho_cpus(binary) != expected:
            raise ValueError('App architecture does not match ' + platform)
        if platform == 'macos-universal':
            for path in app.rglob('*'):
                if path.is_file() and not path.is_symlink():
                    cpus = macho_cpus(path)
                    if cpus is not None and cpus != expected:
                        raise ValueError('Bundled Mach-O lacks both architectures: ' + str(path))
            info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
            if info.get('LSMinimumSystemVersion') != '26.0':
                raise ValueError('Universal preview must declare macOS 26.0 minimum')


def source_version(root=ROOT):
    source = (root / 'Win/stdafx.h').read_text(encoding='latin-1')
    major = int(re.search(r'#define MINEWAYS_MAJOR_VERSION (\d+)', source).group(1))
    minor = int(re.search(r'#define MINEWAYS_MINOR_VERSION (\d+)', source).group(1))
    return f'{major}.{minor:02d}'


def release_notes(tag, root=ROOT):
    if tag != 'v' + source_version(root):
        raise ValueError('Release tag must match the Windows/source version: v' + source_version(root))
    changelog = (root / 'CHANGELOG.md').read_text(encoding='utf-8')
    section = re.search(r'^## \[' + re.escape(tag) + r'\](?:[ \t]+-[^\n]*)?[ \t]*\n(.*?)(?=^## \[|\Z)',
                        changelog, re.MULTILINE | re.DOTALL)
    if section is None:
        raise ValueError('Release requires a CHANGELOG.md section for ' + tag)
    notes = section[1].strip()
    if not any(line.strip() and not line.lstrip().startswith('#') for line in notes.splitlines()):
        raise ValueError('Release changelog section is empty: ' + tag)
    return notes + '\n'


def package(app, platform, output, revision, root=ROOT, distribution='preview', release_tag=None):
    if distribution not in ('preview', 'release'):
        raise ValueError('Unknown distribution: ' + distribution)
    if release_tag:
        if distribution != 'release':
            raise ValueError('A release tag requires the release distribution')
        release_notes(release_tag, root)
    version = source_version(root)
    check_architecture(app, platform)
    if distribution == 'release' and platform.startswith('macos-'):
        info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
        if any(info.get(key) != version for key in ('CFBundleVersion', 'CFBundleShortVersionString')):
            raise ValueError('App bundle version differs from the Windows/source version')
    if output.exists():
        raise FileExistsError('Refusing to overwrite ' + str(output))
    from make_preview_demo import make_demo
    from make_atm10_showcase import make_showcase
    with tempfile.TemporaryDirectory(prefix='mineways-package-') as temporary:
        stage = Path(temporary) / ('Mineways-preview-' + platform if distribution == 'preview' else 'Mineways')
        stage.mkdir()
        if platform == 'windows-x64':
            shutil.copy2(app, stage / 'Mineways.exe')
        else:
            shutil.copytree(app, stage / 'Mineways.app', symlinks=True)
        tools = stage / 'tools'
        tools.mkdir()
        for source in [root / 'tools/import_mod_assets.py', root / 'tools/download_mod_assets.py',
                       root / 'tools/prepare_atm10_showcase.py', root / 'tools/prepare_modpack.py',
                       root / 'tools/showcase_runtime_pins.json',
                       *sorted((root / 'tools').glob('mod_*.py'))]:
            shutil.copy2(source, tools / source.name)
        (tools / 'fixtures').mkdir()
        shutil.copy2(root / 'tools/fixtures/atm10-showcase-1.21.1.json', tools / 'fixtures')
        if platform == 'windows-x64':
            shutil.copy2(root / 'tools/Open ATM10 Showcase.cmd', stage)
            for name in ('launch_atm10_windows.ps1', 'run_mod_helper_windows.ps1', 'prepare_modpack_windows.ps1'):
                shutil.copy2(root / 'tools' / name, tools)
        else:
            launcher = stage / 'Open ATM10 Showcase.command'
            shutil.copy2(root / 'tools/Open ATM10 Showcase.command', launcher)
            launcher.chmod(0o755)
            for name in ('run_mod_helper.command', 'prepare_modpack.command'):
                helper = tools / name
                shutil.copy2(root / 'tools' / name, helper)
                helper.chmod(0o755)
        if distribution == 'release' and platform.startswith('macos-'):
            bundled_tools = stage / 'Mineways.app/Contents/Resources/tools'
            shutil.copytree(tools, bundled_tools)
            subprocess.run(['codesign', '--force', '--sign', '-', str(stage / 'Mineways.app')], check=True)
            subprocess.run(['codesign', '--verify', '--deep', '--strict', str(stage / 'Mineways.app')], check=True)
        shutil.copy2(root / 'docs/preview-quickstart.md', stage / 'QUICKSTART.md')
        shutil.copy2(root / 'docs/atm10-showcase.md', stage / 'ATM10-SHOWCASE.md')
        shutil.copy2(root / 'docs/mod-support.md', stage / 'mod-support.md')
        shutil.copy2(root / 'license.txt', stage / 'license.txt')
        make_demo(stage / 'demo')
        make_showcase(stage / 'atm10-showcase')
        metadata = {'repository': 'wbreiler/Mineways', 'source_revision': revision,
                    'upstream_version': version, 'version': version,
                    'platform': platform, 'configuration': 'Release',
                    'distribution': distribution, 'preview': distribution == 'preview',
                    'release_tag': release_tag}
        if platform == 'macos-universal':
            metadata.update(architectures=['arm64', 'x86_64'], minimum_macos='26.0')
        (stage / 'BUILD-METADATA.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        paths = sorted(path for path in stage.rglob('*') if path.is_symlink() or path.is_file())
        checksums = ''.join(digest(path) + '  ' + path.relative_to(stage).as_posix() + '\n' for path in paths)
        (stage / 'SHA256SUMS').write_text(checksums, encoding='utf-8')
        paths.append(stage / 'SHA256SUMS')
        output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for path in paths:
                name = path.relative_to(stage.parent if distribution == 'preview' else stage).as_posix()
                if path.is_symlink():
                    info = zipfile.ZipInfo(name)
                    info.create_system = 3
                    info.external_attr = (stat.S_IFLNK | 0o777) << 16
                    archive.writestr(info, os.fsencode(os.readlink(path)))
                else:
                    archive.write(path, name)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=PLATFORMS)
    parser.add_argument('--app', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--source-revision')
    parser.add_argument('--distribution', choices=('preview', 'release'), default='preview')
    parser.add_argument('--release-tag')
    parser.add_argument('--check-release', metavar='TAG', help='validate the release tag/changelog and print its notes')
    args = parser.parse_args()
    if args.check_release:
        print(release_notes(args.check_release), end='')
        return
    if not all((args.platform, args.app, args.output)):
        parser.error('--platform, --app, and --output are required to package a build')
    revision = args.source_revision or subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        parser.error('source revision must be a full lowercase Git SHA')
    print(package(args.app.resolve(), args.platform, args.output.resolve(), revision,
                  distribution=args.distribution, release_tag=args.release_tag))


if __name__ == '__main__':
    main()
