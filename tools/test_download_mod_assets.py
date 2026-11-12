"""Downloader tests use original bytes and mocked HTTPS responses, without network access."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

SPEC = importlib.util.spec_from_file_location('download_mod_assets', Path(__file__).with_name('download_mod_assets.py'))
mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)


class Response(io.BytesIO):
    def __init__(self, data, url='https://example.test/asset.jar', length=None):
        super().__init__(data)
        self.headers = {} if length is None else {'Content-Length': str(length)}
        self.url = url

    def geturl(self):
        return self.url


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.payload = b'original fixture bytes'
        self.entry = dict(path='mods/fixture.jar', url='https://example.test/fixture.jar',
                          size=len(self.payload), sha1=hashlib.sha1(self.payload).hexdigest())

    def tearDown(self):
        self.temporary.cleanup()

    def manifest(self, files=None):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps({'format': 1, 'files': files or [self.entry]}))
        return path

    def test_pinned_public_catalog(self):
        files = mod.read_manifest(Path(__file__).parent / 'fixtures/atm10-1.21.1.json')
        self.assertEqual(len(files), 496)
        self.assertEqual(sum('curseforge_file_id' in entry for entry in files), 495)
        self.assertTrue(files[0]['url'].startswith('https://piston-data.mojang.com/'))
        self.assertEqual(sum(entry['size'] for entry in files), 1466394488)

    def test_download_hash_and_reuse(self):
        with patch.object(mod.urllib.request, 'urlopen', return_value=Response(self.payload, length=len(self.payload))) as request:
            sources = mod.download_manifest(self.manifest(), self.root / 'output', workers=1)
            target = self.root / 'output/mods/fixture.jar'
            self.assertEqual(target.read_bytes(), self.payload)
            self.assertEqual(sources.read_text().splitlines(), [str(target.resolve())])
            mod.download_manifest(self.manifest(), self.root / 'output', workers=1)
            self.assertEqual(request.call_count, 1)

    def test_invalid_download_preserves_previous_file(self):
        target = self.root / 'mods/fixture.jar'
        target.parent.mkdir()
        target.write_bytes(b'previous data')
        bad_responses = [Response(self.payload[:-1]), Response(self.payload + b'x'),
                         Response(b'X' * len(self.payload)), Response(self.payload, length=1),
                         Response(self.payload, url='http://example.test/asset.jar')]
        for response in bad_responses:
            with patch.object(mod.urllib.request, 'urlopen', return_value=response):
                with self.assertRaises(ValueError):
                    mod.download_file(self.entry, self.root)
            self.assertEqual(target.read_bytes(), b'previous data')
            self.assertEqual(list(target.parent.glob('.download-*')), [])

    def test_destination_escape_reports_paths(self):
        destination = self.root.resolve()
        outside = destination.parent / 'outside.jar'
        target = destination / 'mods/fixture.jar'
        original_resolve = Path.resolve

        def resolve(path, *args, **kwargs):
            return outside if path == target else original_resolve(path, *args, **kwargs)

        with patch.object(Path, 'resolve', resolve):
            with patch.object(mod.urllib.request, 'urlopen') as request:
                with self.assertRaises(ValueError) as caught:
                    mod.download_file(self.entry, destination)
        message = str(caught.exception)
        for detail in ['download path escapes destination', repr(self.entry['path']),
                       repr(str(destination)), repr(str(target)), repr(str(outside))]:
            self.assertIn(detail, message)
        request.assert_not_called()
        self.assertFalse(target.parent.exists())

    def test_retries_are_bounded(self):
        with patch.object(mod.urllib.request, 'urlopen', side_effect=urllib.error.URLError('offline')) as request:
            with patch.object(mod.time, 'sleep'):
                with self.assertRaisesRegex(RuntimeError, 'download failed'):
                    mod.download_file(self.entry, self.root)
        self.assertEqual(request.call_count, 3)

    def test_source_list_symlink_is_rejected(self):
        destination = self.root / 'output'
        (destination / 'mods').mkdir(parents=True)
        (destination / 'mods/fixture.jar').write_bytes(self.payload)
        outside = self.root / 'outside.txt'
        outside.write_text('unchanged')
        try:
            (destination / 'sources.txt').symlink_to(outside)
        except OSError as error:
            if getattr(error, 'winerror', None) == 1314:
                self.skipTest('Windows account lacks symbolic-link privilege')
            raise
        with self.assertRaisesRegex(ValueError, 'symbolic link'):
            mod.download_manifest(self.manifest(), destination, workers=1)
        self.assertEqual(outside.read_text(), 'unchanged')

    def test_paths_and_urls(self):
        for name in ['../outside.jar', '/absolute.jar', 'C:/drive.jar', 'a\\b.jar', 'a//b.jar',
                     'a/./b.jar', 'mods/NUL.jar', 'mods/bad?.jar', 'mods/trailing./x.jar', 'mods/file.txt']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                mod.read_manifest(self.manifest([dict(self.entry, path=name)]))
        with self.assertRaises(ValueError):
            mod.read_manifest(self.manifest([self.entry, dict(self.entry, path='MODS/FIXTURE.JAR')]))
        for url in ['http://example.test/a.jar', 'file:///a.jar', 'https://user:password@example.test/a.jar']:
            with self.subTest(url=url), self.assertRaises(ValueError):
                mod.read_manifest(self.manifest([dict(self.entry, url=url)]))

    def test_limits_and_digest(self):
        for changes in [{'size': 0}, {'size': mod.MAX_FILE + 1}, {'size': True}, {'sha1': 'bad'}]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                mod.read_manifest(self.manifest([dict(self.entry, **changes)]))
        files = [dict(self.entry, path=f'mods/{i}.jar', size=mod.MAX_FILE) for i in range(17)]
        with self.assertRaisesRegex(ValueError, 'total size'):
            mod.read_manifest(self.manifest(files))
        with patch.object(mod.time, 'monotonic', side_effect=[0, 181]):
            with patch.object(mod.urllib.request, 'urlopen', return_value=Response(self.payload)):
                with self.assertRaisesRegex(ValueError, 'time limit'):
                    mod.download_file(self.entry, self.root)


if __name__ == '__main__':
    unittest.main()
