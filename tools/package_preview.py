#!/usr/bin/env python3
"""Create a portable preview ZIP from an already built executable or app bundle."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ('windows-x64', 'macos-x86_64', 'macos-arm64')


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
        else:
            expected_cpu = 0x100000c if platform == 'macos-arm64' else 0x1000007
            if header[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack_from('<I', header, 4)[0] != expected_cpu:
                raise ValueError('App architecture does not match ' + platform)


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
