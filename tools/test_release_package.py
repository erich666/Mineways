#!/usr/bin/env python3
"""Check regular ZIP layout, version guards, and bundled Mac picker staging."""
import json
import os
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from package_preview import MAC_CPUS, ROOT, package, release_notes, source_version
from test_preview_package import extract_package


class ReleasePackageTests(unittest.TestCase):
    def test_exact_version_and_nonempty_changelog(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'Win').mkdir()
            (root / 'Win/stdafx.h').write_text('#define MINEWAYS_MAJOR_VERSION 53\n#define MINEWAYS_MINOR_VERSION 9\n')
            self.assertEqual(source_version(root), '53.09')
            for heading in ('## [v53.09] - Unreleased', '## [v53.09] - 2026-11-14'):
                (root / 'CHANGELOG.md').write_text(heading + '\n\n* The actual release notes.\n\n## [v52.00]\nOld notes\n')
                self.assertEqual(release_notes('v53.09', root), '* The actual release notes.\n')
            with self.assertRaisesRegex(ValueError, 'match'):
                release_notes('v53.9', root)
            for contents in ('## [v53.090]\nWrong version notes\n', '## [v53.09]\n\n### Fixed\n'):
                (root / 'CHANGELOG.md').write_text(contents)
                with self.assertRaises(ValueError):
                    release_notes('v53.09', root)

    def test_regular_and_preview_layouts(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            app = directory / 'Mineways.exe'
            header = bytearray(64)
            header[:2] = b'MZ'
            struct.pack_into('<I', header, 60, 64)
            app.write_bytes(header + b'PE\0\0\x64\x86')
            for distribution in ('release', 'preview'):
                archive = directory / (distribution + '.zip')
                package(app, 'windows-x64', archive, 'a' * 40, distribution=distribution)
                extracted = directory / ('extracted ' + distribution)
                root = extract_package(archive, extracted)
                self.assertEqual(root, extracted if distribution == 'release' else extracted / 'Mineways-preview-windows-x64')
                self.assertTrue((root / 'Mineways.exe').is_file())
                self.assertTrue((root / 'tools/import_mod_assets.py').is_file())
                self.assertTrue((root / 'demo/world/level.dat').is_file())
                metadata = json.loads((root / 'BUILD-METADATA.json').read_text())
                self.assertEqual(metadata['distribution'], distribution)
                self.assertEqual(metadata['preview'], distribution == 'preview')
                self.assertEqual(metadata['version'], source_version())
                self.assertIsNone(metadata['release_tag'])

    def test_regular_mac_bundles_tools_before_signing(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            app = directory / 'Mineways.app'
            (app / 'Contents/MacOS').mkdir(parents=True)
            fat = b'\xca\xfe\xba\xbe' + struct.pack('>I', 2) + b''.join(
                struct.pack('>IIIII', cpu, 0, 0, 0, 0) for cpu in MAC_CPUS.values())
            (app / 'Contents/MacOS/mineways').write_bytes(fat)
            info = {'LSMinimumSystemVersion': '26.0', 'CFBundleVersion': source_version(),
                    'CFBundleShortVersionString': source_version()}
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
            commands = []
            def sign(command, check):
                tools = Path(command[-1]) / 'Contents/Resources/tools'
                self.assertEqual((tools / 'prepare_modpack.py').read_bytes(), (ROOT / 'tools/prepare_modpack.py').read_bytes())
                if os.name != 'nt':
                    self.assertTrue((tools / 'prepare_modpack.command').stat().st_mode & 0o111)
                commands.append(command[:3])
            archive = directory / 'Mineways-macOS.zip'
            with patch('package_preview.subprocess.run', side_effect=sign):
                package(app, 'macos-universal', archive, 'a' * 40, distribution='release')
            self.assertEqual(commands, [['codesign', '--force', '--sign'], ['codesign', '--verify', '--deep']])
            root = extract_package(archive, directory / 'output with spaces')
            self.assertTrue((root / 'Mineways.app/Contents/Resources/tools/prepare_modpack.command').is_file())
            self.assertFalse((app / 'Contents/Resources/tools').exists())
            info['CFBundleVersion'] = '0.00'
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
            with self.assertRaisesRegex(ValueError, 'bundle version'):
                package(app, 'macos-universal', directory / 'bad.zip', 'a' * 40, distribution='release')

    def test_flat_archive_symlink_cannot_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('BUILD-METADATA.json', '{}')
                stream.writestr('SHA256SUMS', '')
                info = zipfile.ZipInfo('Mineways.app/outside')
                info.external_attr = (0o120777 << 16)
                stream.writestr(info, '../../outside')
            with self.assertRaises(ValueError):
                extract_package(archive, root / 'extracted')


if __name__ == '__main__':
    unittest.main()
