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


def package(app, platform, output, revision, root=ROOT):
    check_architecture(app, platform)
    if output.exists():
        raise FileExistsError('Refusing to overwrite ' + str(output))
    from make_preview_demo import make_demo
    with tempfile.TemporaryDirectory(prefix='mineways-package-') as temporary:
        stage = Path(temporary) / ('Mineways-preview-' + platform)
        stage.mkdir()
        if platform == 'windows-x64':
            shutil.copy2(app, stage / 'Mineways.exe')
        else:
            shutil.copytree(app, stage / 'Mineways.app', symlinks=True)
        tools = stage / 'tools'
        tools.mkdir()
        for source in [root / 'tools/import_mod_assets.py', *sorted((root / 'tools').glob('mod_*.py'))]:
            shutil.copy2(source, tools / source.name)
        shutil.copy2(root / 'docs/preview-quickstart.md', stage / 'QUICKSTART.md')
        shutil.copy2(root / 'docs/mod-support.md', stage / 'mod-support.md')
        shutil.copy2(root / 'license.txt', stage / 'license.txt')
        make_demo(stage / 'demo')
        version_source = (root / 'Win/stdafx.h').read_text(encoding='latin-1')
        major = int(re.search(r'#define MINEWAYS_MAJOR_VERSION (\d+)', version_source).group(1))
        minor = int(re.search(r'#define MINEWAYS_MINOR_VERSION (\d+)', version_source).group(1))
        metadata = {'repository': 'wbreiler/Mineways', 'source_revision': revision,
                    'upstream_version': f'{major}.{minor:02d}',
                    'platform': platform, 'configuration': 'Release', 'preview': True}
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
                name = path.relative_to(stage.parent).as_posix()
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
    parser.add_argument('--platform', choices=PLATFORMS, required=True)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-revision')
    args = parser.parse_args()
    revision = args.source_revision or subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        parser.error('source revision must be a full lowercase Git SHA')
    print(package(args.app.resolve(), args.platform, args.output.resolve(), revision))


if __name__ == '__main__':
    main()
