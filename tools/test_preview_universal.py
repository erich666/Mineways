#!/usr/bin/env python3
"""Check universal bundle alignment and architecture guards without Apple tools."""
import os
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest.mock import patch

from merge_preview_macos import dependency_contract, validate_bundles
from package_preview import MAC_CPUS, check_architecture


def thin(cpu):
    return b'\xcf\xfa\xed\xfe' + struct.pack('<I', cpu) + bytes(24)


def universal():
    return b'\xca\xfe\xba\xbe' + struct.pack('>I', 2) + b''.join(
        struct.pack('>IIIII', cpu, 0, 0, 0, 0) for cpu in MAC_CPUS.values())


class UniversalTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.arm = Path(self.temporary.name) / 'arm.app'
        self.intel = Path(self.temporary.name) / 'intel.app'
        for app, cpu in ((self.arm, MAC_CPUS['arm64']), (self.intel, MAC_CPUS['x86_64'])):
            for name in ('MacOS', 'Frameworks', 'Resources'):
                (app / 'Contents' / name).mkdir(parents=True)
            (app / 'Contents/MacOS/mineways').write_bytes(thin(cpu))
            (app / 'Contents/Frameworks/libexample.1.dylib').write_bytes(thin(cpu))
            (app / 'Contents/Resources/icon').write_bytes(b'original icon')
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleVersion': '14.03'}))

    def test_matching_native_bundles(self):
        self.assertEqual(validate_bundles(self.arm, self.intel),
                         ['Contents/Frameworks/libexample.1.dylib', 'Contents/MacOS/mineways'])

    def test_missing_library(self):
        (self.intel / 'Contents/Frameworks/libexample.1.dylib').unlink()
        with self.assertRaisesRegex(ValueError, 'inventories differ'):
            validate_bundles(self.arm, self.intel)

    def test_resource_and_version_conflicts(self):
        for name in ('Resources/icon', 'Info.plist'):
            with self.subTest(name=name):
                path = self.intel / 'Contents' / name
                original = path.read_bytes()
                path.write_bytes(b'conflicting build')
                with self.assertRaisesRegex(ValueError, 'resources or versions differ'):
                    validate_bundles(self.arm, self.intel)
                path.write_bytes(original)

    @unittest.skipIf(os.name == 'nt', 'Symlink test runs on macOS')
    def test_symlink_conflict(self):
        for app in (self.arm, self.intel):
            (app / 'Contents/Frameworks/libexample.dylib').symlink_to('libexample.1.dylib')
        self.assertEqual(len(validate_bundles(self.arm, self.intel)), 2)
        link = self.intel / 'Contents/Frameworks/libexample.dylib'
        link.unlink()
        link.symlink_to('missing.dylib')
        with self.assertRaisesRegex(ValueError, 'symlinks differ'):
            validate_bundles(self.arm, self.intel)

    def test_fat_dependency_contract_checks_both_slices(self):
        binary = self.arm / 'Contents/MacOS/mineways'
        binary.write_bytes(universal())
        for mismatch in (False, True):
            with self.subTest(mismatch=mismatch):
                checked = []
                def output(args):
                    if args[0] == 'vtool':
                        return ' minos 26.0\n minos 26.0\n'
                    architecture = args[args.index('-arch') + 1]
                    checked.append(architecture)
                    dependency = 'different' if mismatch and architecture == 'x86_64' else 'shared'
                    return str(binary) + ':\n\t@executable_path/' + dependency + '.dylib (compatibility version 1.0.0)\n'
                with patch('merge_preview_macos.command', side_effect=output):
                    if mismatch:
                        with self.assertRaisesRegex(ValueError, 'dependency versions differ'):
                            dependency_contract(binary)
                    else:
                        self.assertEqual(len(dependency_contract(binary)), 1)
                self.assertEqual(set(checked), {'arm64', 'x86_64'})

    def test_universal_requires_every_library_slice(self):
        (self.arm / 'Contents/MacOS/mineways').write_bytes(universal())
        (self.arm / 'Contents/Info.plist').write_bytes(plistlib.dumps({'LSMinimumSystemVersion': '26.0'}))
        with self.assertRaisesRegex(ValueError, 'lacks both architectures'):
            check_architecture(self.arm, 'macos-universal')
        (self.arm / 'Contents/Frameworks/libexample.1.dylib').write_bytes(universal())
        check_architecture(self.arm, 'macos-universal')


if __name__ == '__main__':
    unittest.main()
