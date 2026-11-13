#!/usr/bin/env python3
"""Merge matching native preview packages into one ad-hoc signed universal app."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

from package_preview import MAC_CPUS, check_architecture, macho_cpus
from test_preview_package import extract_package


def bundle_files(app):
    return {p.relative_to(app).as_posix(): p for p in app.rglob('*')
            if (p.is_file() or p.is_symlink()) and '_CodeSignature' not in p.parts}


def validate_bundles(arm, intel):
    a, b = bundle_files(arm), bundle_files(intel)
    if a.keys() != b.keys():
        raise ValueError('Bundle file inventories differ: ' + ', '.join(sorted(a.keys() ^ b.keys())))
    binaries = []
    for name in sorted(a):
        left, right = a[name], b[name]
        if left.is_symlink() or right.is_symlink():
            if not (left.is_symlink() and right.is_symlink()) or os.readlink(left) != os.readlink(right):
                raise ValueError('Bundle symlinks differ: ' + name)
        else:
            first, second = macho_cpus(left), macho_cpus(right)
            if first is not None or second is not None:
                if first != {MAC_CPUS['arm64']} or second != {MAC_CPUS['x86_64']}:
                    raise ValueError('Unexpected native Mach-O architectures: ' + name)
                binaries.append(name)
            elif left.read_bytes() != right.read_bytes():
                raise ValueError('Bundle resources or versions differ: ' + name)
    if 'Contents/MacOS/mineways' not in binaries:
        raise ValueError('Bundles do not contain the native Mineways executable')
    return binaries


def command(args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT, timeout=60)


def dependency_contract(binary):
    lines = command(['otool', '-L', str(binary)]).splitlines()[1:]
    bundled = []
    for line in lines:
        line = line.strip()
        if line.startswith('@'):
            bundled.append(line)
        elif not line.startswith(('/usr/lib/', '/System/Library/')):
            raise ValueError('External developer dependency: ' + line)
    build = command(['vtool', '-show-build', str(binary)])
    minimums = re.findall(r'^\s*minos\s+(\d+)\.(\d+)', build, re.MULTILINE)
    if not minimums or any((int(a), int(b)) > (26, 0) for a, b in minimums):
        raise ValueError('Mach-O requires a system newer than macOS 26.0: ' + str(binary))
    return sorted(bundled)


def merge_packages(arm_package, intel_package, output, revision):
    if output.exists():
        raise FileExistsError('Refusing to overwrite ' + str(output))
    with tempfile.TemporaryDirectory(prefix='mineways-universal-') as temporary:
        temporary = Path(temporary)
        arm = extract_package(arm_package, temporary / 'arm')
        intel = extract_package(intel_package, temporary / 'intel')
        a = json.loads((arm / 'BUILD-METADATA.json').read_text())
        b = json.loads((intel / 'BUILD-METADATA.json').read_text())
        if (a['platform'], b['platform']) != ('macos-arm64', 'macos-x86_64'):
            raise ValueError('Expected ARM64 and Intel native packages in that order')
        if a['source_revision'] != revision or b['source_revision'] != revision:
            raise ValueError('Native packages do not match the requested source revision')
        if {k: v for k, v in a.items() if k != 'platform'} != {k: v for k, v in b.items() if k != 'platform'}:
            raise ValueError('Native package build metadata differs')
        arm_app, intel_app = arm / 'Mineways.app', intel / 'Mineways.app'
        binaries = validate_bundles(arm_app, intel_app)
        for name in binaries:
            if dependency_contract(arm_app / name) != dependency_contract(intel_app / name):
                raise ValueError('Bundled dependency versions differ: ' + name)
        staged = temporary / 'Mineways.app'
        shutil.copytree(arm_app, staged, symlinks=True)
        shutil.rmtree(staged / 'Contents/_CodeSignature', ignore_errors=True)
        for name in binaries:
            command(['lipo', '-create', str(arm_app / name), str(intel_app / name), '-output', str(staged / name)])
        info_path = staged / 'Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        info['LSMinimumSystemVersion'] = '26.0'
        info_path.write_bytes(plistlib.dumps(info))
        check_architecture(staged, 'macos-universal')
        command(['codesign', '--force', '--deep', '--sign', '-', str(staged)])
        command(['codesign', '--verify', '--deep', '--strict', str(staged)])
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(staged, output, symlinks=True)
    print(f'Merged {len(binaries)} Mach-O files with arm64 and x86_64 slices: {output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm-package', type=Path, required=True)
    parser.add_argument('--intel-package', type=Path, required=True)
    parser.add_argument('--output-app', type=Path, required=True)
    parser.add_argument('--source-revision', required=True)
    args = parser.parse_args()
    merge_packages(args.arm_package.resolve(), args.intel_package.resolve(), args.output_app.resolve(), args.source_revision)


if __name__ == '__main__':
    main()
