"""End-to-end tests for data-only mod asset imports. No Minecraft install needed."""
import importlib.util
import json
import re
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/import_mod_assets.py'
if not SCRIPT.exists():
    SCRIPT = Path(__file__).with_name('import_mod_assets.py')
spec = importlib.util.spec_from_file_location('import_mod_assets', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def put(root, path, data):
    path = root / path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else json.dumps(data).encode())


def simple(root, namespace='sample', color=(16, 64, 128, 255)):
    put(root, f'assets/{namespace}/textures/block/stone.png', mod.encode_png(2, 2, bytes(color) * 4))
    put(root, f'assets/{namespace}/models/block/stone.json',
        {'parent': 'minecraft:block/cube_all', 'textures': {'all': f'{namespace}:block/stone'}})
    put(root, f'assets/{namespace}/blockstates/stone.json', {'variants': {'': {'model': f'{namespace}:block/stone'}}})


def adapter_assets(root):
    """Original test art with the exact resource identifiers used by verified adapters."""
    for name in ('matrix_frame', 'drive', 'quartz_glass', 'quartz_vibrant_glass', 'spatial_pylon'):
        model = 'quartz_glass' if name == 'quartz_vibrant_glass' else name
        put(root, f'assets/ae2/models/block/{model}.json', {})
        put(root, f'assets/ae2/blockstates/{name}.json', {'variants': {'': {'model': f'ae2:block/{model}'}}})
    put(root, 'assets/ae2/textures/block/drive.png', mod.encode_png(2, 2, bytes((80, 100, 120, 255)) * 4))
    put(root, 'assets/ae2/models/block/drive/drive_base.json', {'elements': [
        {'from': [0, 0, 0], 'to': [12, 10, 16], 'faces': {side: {'texture': 'ae2:block/drive'} for side in mod.DIRECTIONS}}]})
    put(root, 'assets/ae2/blockstates/drive.json', {'variants': {
        'facing=north,spin=0': {'model': 'ae2:block/drive'},
        'facing=north,spin=1': {'model': 'ae2:block/drive', 'ae2:z': 270},
        'facing=north,spin=2': {'model': 'ae2:block/drive', 'ae2:z': 180}}})
    put(root, 'assets/ae2/textures/block/glass/quartz_glass_a.png', mod.encode_png(4, 4, bytes((0, 0, 255, 128)) * 16))
    frame = bytes(channel for y in range(4) for x in range(4)
                  for channel in ((255, 0, 0, 255) if x in (0, 3) or y in (0, 3) else (0, 0, 0, 0)))
    put(root, 'assets/ae2/textures/block/glass/quartz_glass_frame1111.png', mod.encode_png(4, 4, frame))
    put(root, 'assets/ae2/textures/block/spatial_pylon/base.png', mod.encode_png(2, 2, bytes((128, 128, 128, 255)) * 4))
    put(root, 'assets/ae2/textures/block/spatial_pylon/dim.png', mod.encode_png(2, 2, bytes((0, 0, 255, 128)) + bytes((0, 0, 0, 0)) * 3))
    put(root, 'assets/allthemodium/models/block/fluid/molten.json', {'textures': {'particle': 'allthemodium:block/fluid/molten_still'}})
    put(root, 'assets/allthemodium/blockstates/molten_allthemodium_block.json', {'variants': {'': {'model': 'allthemodium:block/fluid/molten'}}})
    put(root, 'assets/allthemodium/textures/block/fluid/atm_molten_still.png', mod.encode_png(2, 2, bytes((255, 180, 10, 255)) * 4))
    put(root, 'assets/allthemodium/textures/block/fluid/atm_molten_flow.png', mod.encode_png(2, 2, bytes((255, 100, 10, 255)) * 4))


def build_fixture(root):
    """Create original assets for geometry/UV, alpha, state and schematic tests."""
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    source = root / 'assets-source'
    for index, direction in enumerate(mod.DIRECTIONS):
        # Distinct corners expose mirrored/rotated UVs, unlike a solid texture.
        pixels = bytearray()
        size = 32 if direction == 'east' else 16
        for y in range(size):
            for x in range(size):
                pixels.extend(((index + 1) * 30, x * 256 // size, y * 256 // size, 255))
        put(source, f'assets/fixture/textures/block/{direction}.png', mod.encode_png(size, size, pixels))
    faces = {direction: {'texture': 'fixture:block/' + direction, 'cullface': direction} for direction in mod.DIRECTIONS}
    put(source, 'assets/fixture/models/block/cube.json', {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': faces}]})
    put(source, 'assets/fixture/blockstates/cube.json', {'variants': {'': {'model': 'fixture:block/cube'}}})
    put(source, 'assets/fixture/models/block/wedge.json', {'elements': [{
        'from': [2, 1, 3], 'to': [14, 9, 11],
        'rotation': {'axis': 'z', 'angle': 22.5, 'origin': [8, 8, 8], 'rescale': True},
        'faces': {direction: {'texture': 'fixture:block/' + direction} for direction in mod.DIRECTIONS}}]})
    put(source, 'assets/fixture/blockstates/wedge.json', {'variants': {
        'facing=east': {'model': 'fixture:block/wedge', 'x': 90, 'y': 90, 'uvlock': True},
        'facing=west': {'model': 'fixture:block/wedge', 'x': 0, 'y': 270}}})
    pixels = b''.join(bytes((255, 255, 255, 255 if x % 2 else 0)) for y in range(16) for x in range(16))
    put(source, 'assets/fixture/textures/block/panel.png', mod.encode_png(16, 16, pixels))
    put(source, 'assets/fixture/models/block/panel.json', {'elements': [{'from': [0, 0, 8], 'to': [16, 16, 8], 'faces': {
        'north': {'texture': 'fixture:block/panel', 'uv': [0, 0, 16, 16]},
        'south': {'texture': 'fixture:block/panel', 'uv': [16, 0, 0, 16]}}}]})
    put(source, 'assets/fixture/blockstates/panel.json', {'variants': {'': {'model': 'fixture:block/panel'}}})
    put(source, 'assets/fixture/models/block/bright.json', {'parent': 'minecraft:block/cube_all', 'textures': {'all': 'fixture:block/east'}})
    put(source, 'assets/fixture/blockstates/lamp.json', {'variants': {'lit=false': {'model': 'fixture:block/cube'}, 'lit=true': {'model': 'fixture:block/bright'}}})
    long_name = 'long/' * 60 + 'cube'
    long_archive = root / 'long-names.jar'
    with zipfile.ZipFile(long_archive, 'w') as archive:
        archive.writestr(f'assets/fixture/blockstates/{long_name}.json', json.dumps({'variants': {'': {'model': 'fixture:block/cube'}}}))
    put(source, 'data/fixture/dimension/moon.json', {'type': {'min_y': -64, 'height': 384, 'coordinate_scale': 2}})
    adapter_assets(source)
    pack = root / 'pack'
    mod.compile_pack([source, long_archive], pack)
    return pack


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mineways-mod-assets-')
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def compile(self, name='out', sources=None):
        output = self.root / name
        report = mod.compile_pack(sources or [self.source], output)
        return output, report

    def test_namespaces_overrides_determinism_and_colors(self):
        simple(self.source)
        simple(self.source, 'other', (255, 0, 0, 255))
        override = self.root / 'override'
        put(override, 'assets/sample/textures/block/stone.png', mod.encode_png(2, 2, bytes((0, 255, 0, 255)) * 4))
        first, report = self.compile(sources=[self.source, override])
        second, report2 = self.compile('again', [self.source, override])
        text = (first / 'manifest.mwpack').read_text()
        self.assertIn('state\tother:stone\tff0000\t255', text)
        self.assertIn('state\tsample:stone\t00ff00\t255', text)
        self.assertEqual(report['blocks'], 2)
        self.assertEqual(report, report2)
        for path in first.rglob('*'):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())

    def test_archive_and_assets_directory(self):
        simple(self.source)
        jar = self.root / 'mod.jar'
        with zipfile.ZipFile(jar, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in self.source.rglob('*'):
                if path.is_file():
                    archive.write(path, path.relative_to(self.source).as_posix())
            archive.writestr('mod/Main.class', b'not executable')
        out, report = self.compile(sources=[jar])
        other, _ = self.compile('dir', [self.source / 'assets'])
        self.assertEqual(report['blocks'], 1)
        self.assertEqual((out / 'manifest.mwpack').read_bytes(), (other / 'manifest.mwpack').read_bytes())
        self.assertFalse((out / 'mod').exists())

    def test_parent_aliases_uv_rotation_and_multipart(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/parent.json', {
            'textures': {'first': '#second'},
            'elements': [{'from': [1, 2, 3], 'to': [9, 10, 11],
                          'rotation': {'axis': 'x', 'angle': 22.5, 'origin': [8, 8, 8], 'rescale': True},
                          'faces': {'north': {'texture': '#first', 'rotation': 90}}}]})
        put(self.source, 'assets/sample/models/block/child.json', {
            'parent': 'sample:block/parent', 'textures': {'second': 'sample:block/stone'}})
        put(self.source, 'assets/sample/blockstates/device.json', {'multipart': [
            {'apply': {'model': 'sample:block/stone'}},
            {'when': {'OR': [{'powered': 'true'}, {'facing': 'north|south'}]},
             'apply': [{'model': 'sample:block/child', 'x': 90, 'y': 270, 'uvlock': True}]}]})
        out, report = self.compile()
        text = (out / 'manifest.mwpack').read_text()
        self.assertRegex(text, r'usemodel\t\d+\t90\t270\t1')
        self.assertIn('element\t1\t2\t3\t9\t10\t11\tx\t22.5\t8\t8\t8\t1', text)
        self.assertIn('face\tnorth\t7\t6\t15\t14\t90', text)
        self.assertIn('sample:device[facing=north,powered=false]', text)
        self.assertEqual(report['selectors'], 6)
        self.assertIn('state\tsample:device\t', text)
        self.assertTrue(any('Weighted' in w for w in report['warnings']))

    def test_rejects_unsafe_archives(self):
        for index, name in enumerate(('../escape.json', '/absolute.png', 'assets\\bad.png', 'C:/evil.png', 'assets/bad\0.png')):
            jar = self.root / f'bad{index}.zip'
            with zipfile.ZipFile(jar, 'w') as archive:
                info = zipfile.ZipInfo('placeholder')
                # Preserve the malicious spelling on disk on every OS. ZipInfo's
                # constructor otherwise replaces backslashes on Windows.
                info.filename = info.orig_filename = name
                archive.writestr(info, b'{}')
            with zipfile.ZipFile(jar) as archive:
                self.assertEqual(archive.infolist()[0].orig_filename, name)
            with self.assertRaisesRegex(mod.ImportError, 'unsafe'):
                self.compile(f'out{index}', [jar])
        jar = self.root / 'link.zip'
        with zipfile.ZipFile(jar, 'w') as archive:
            info = zipfile.ZipInfo('assets/sample/textures/evil.png')
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(info, '/etc/passwd')
        with self.assertRaisesRegex(mod.ImportError, 'symbolic'):
            self.compile('link', [jar])

    def test_rejects_zip_bomb(self):
        jar = self.root / 'bomb.zip'
        with zipfile.ZipFile(jar, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('assets/sample/models/bomb.json', bytes(2 * 1024 * 1024))
        with self.assertRaisesRegex(mod.ImportError, 'ratio'):
            self.compile(sources=[jar])

    def test_rejects_source_symlink(self):
        simple(self.source)
        try:
            (self.source / 'alias').symlink_to(self.source, target_is_directory=True)
        except OSError as error:
            if getattr(error, 'winerror', None) == 1314:
                self.skipTest('Windows account lacks symbolic-link privilege')
            raise
        with self.assertRaisesRegex(mod.ImportError, 'symbolic'):
            self.compile()

    def test_bad_models_are_reported_without_hiding_good_blocks(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/cycle.json', {'parent': 'sample:block/cycle'})
        put(self.source, 'assets/sample/blockstates/cycle.json', {'variants': {'': {'model': 'sample:block/cycle'}}})
        put(self.source, 'assets/sample/models/block/custom.json', {'loader': 'sample:java'})
        put(self.source, 'assets/sample/blockstates/custom.json', {'variants': {'': {'model': 'sample:block/custom'}}})
        put(self.source, 'assets/sample/blockstates/broken.json', b'{')
        _, report = self.compile()
        self.assertEqual(report['blocks'], 1)
        self.assertEqual(len(report['unsupported']), 3)
        self.assertTrue(any('cyclic' in e['reason'] for e in report['unsupported']))
        self.assertTrue(any('custom model loader' in e['reason'] for e in report['unsupported']))

    def test_invalid_rotations_and_expansion_are_reported(self):
        simple(self.source)
        put(self.source, 'assets/sample/blockstates/bool.json', {'variants': {'': {'model': 'sample:block/stone', 'x': True}}})
        parts = [{'when': {f'flag{i}': 'true'}, 'apply': {'model': 'sample:block/stone'}} for i in range(13)]
        put(self.source, 'assets/sample/blockstates/excessive.json', {'multipart': parts})
        _, report = self.compile()
        self.assertEqual(len(report['unsupported']), 2)
        self.assertTrue(any('4096' in item['reason'] for item in report['unsupported']))

    def test_quarter_turns_wrap_like_real_ae2_models(self):
        simple(self.source)
        put(self.source, 'assets/sample/blockstates/wrapped.json', {'variants': {
            '': {'model': 'sample:block/stone', 'x': 360, 'y': -90}}})
        out, report = self.compile()
        self.assertEqual(report['unsupported'], [])
        self.assertIn('usemodel\t0\t0\t270\t0', (out / 'manifest.mwpack').read_text())
        self.assertEqual(report['geometries'], 1)

    def test_texture_budget_fails_before_output(self):
        simple(self.source)
        simple(self.source, 'other')
        limit = mod.MAX_TEXTURES
        try:
            mod.MAX_TEXTURES = 1
            with self.assertRaisesRegex(mod.ImportError, 'texture catalog exceeds'):
                self.compile()
        finally:
            mod.MAX_TEXTURES = limit
        self.assertFalse((self.root / 'out').exists())

    def test_catalog_exceeds_export_atlas_capacity(self):
        for index in range(916):
            simple(self.source, f'mod_{index}')
        out, report = self.compile()
        self.assertEqual(report['textures'], 916)
        self.assertEqual(report['blocks'], 916)
        self.assertEqual((out / 'manifest.mwpack').read_text().splitlines()[0], 'MINEWAYS_MOD_PACK\t2')

    def test_composite_children_and_partial_states(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/composite.json', {'loader': 'neoforge:composite', 'children': {
            'solid': {'parent': 'sample:block/stone'}, 'glow': {'parent': 'sample:block/stone'}}})
        put(self.source, 'assets/sample/models/block/custom.json', {'loader': 'sample:java'})
        put(self.source, 'assets/sample/blockstates/composite.json', {'variants': {
            'lit=true': {'model': 'sample:block/composite', 'x': 90},
            'lit=false': {'model': 'sample:block/custom'}}})
        out, report = self.compile()
        self.assertEqual(report['blocks'], 2)
        self.assertEqual(report['unsupported'], [])
        self.assertEqual(report['unsupported_states'][0]['state'], 'sample:composite[lit=false]')
        text = (out / 'manifest.mwpack').read_text()
        self.assertRegex(text, r'usemodel\t\d+\t90\t0\t0')
        self.assertEqual(report['elements'], 3)

    def test_partial_failure_cannot_fall_through_generic_selector(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/custom.json', {'loader': 'sample:java'})
        put(self.source, 'assets/sample/blockstates/partial.json', {'variants': {
            '': {'model': 'sample:block/stone'},
            'facing=east': {'model': 'sample:block/custom'},
            'facing=west': {'model': 'sample:block/stone'}}})
        out, report = self.compile()
        text = (out / 'manifest.mwpack').read_text()
        self.assertIn('state\tsample:partial[facing=west]', text)
        self.assertNotIn('state\tsample:partial\t', text)
        self.assertEqual({r['state'] for r in report['unsupported_states']},
                         {'sample:partial', 'sample:partial[facing=east]'})

    def test_missing_alias_has_reported_checkerboard(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/missing.json', {'elements': [
            {'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': {'up': {'texture': '#missing'}}}]})
        put(self.source, 'assets/sample/blockstates/missing.json', {'variants': {'': {'model': 'sample:block/missing'}}})
        out, report = self.compile()
        self.assertEqual(report['blocks'], 2)
        self.assertTrue(any('checkerboard' in warning for warning in report['warnings']))
        self.assertIn('mineways:missing_texture', (out / 'manifest.mwpack').read_text())

    def test_fusion_sheet_and_boolean_multipart(self):
        simple(self.source)
        pixels = bytes(c for y in range(4) for x in range(20) for c in ((255, 0, 0, 255) if x < 4 else (0, 255, 0, 255)))
        put(self.source, 'assets/sample/textures/block/stone.png', mod.encode_png(20, 4, pixels))
        put(self.source, 'assets/sample/textures/block/stone.png.mcmeta', {'fusion': {'type': 'connecting', 'layout': 'pieced'}})
        put(self.source, 'assets/sample/blockstates/bool.json', {'multipart': [
            {'when': {'north': True}, 'apply': {'model': 'sample:block/stone'}}]})
        out, report = self.compile()
        self.assertEqual(report['blocks'], 2)
        self.assertIn('north=true', (out / 'manifest.mwpack').read_text())
        self.assertEqual(mod.decode_png((out / 'textures/0000.png').read_bytes()), (4, 4, bytes((255, 0, 0, 255)) * 16))
        self.assertTrue(any('unconnected tile' in w for w in report['warnings']))

    def test_fusion_full_padded_sheet(self):
        simple(self.source)
        pixels = bytes((255, 0, 0, 255)) + bytes((0, 255, 0, 255)) * 63
        put(self.source, 'assets/sample/textures/block/stone.png', mod.encode_png(8, 8, pixels))
        put(self.source, 'assets/sample/textures/block/stone.png.mcmeta', {'fusion': {'type': 'connecting', 'layout': 'full'}})
        out, report = self.compile()
        self.assertEqual(mod.decode_png((out / 'textures/0000.png').read_bytes()), (1, 1, bytes((255, 0, 0, 255))))
        self.assertEqual(report['blocks'], 1)
        put(self.source, 'assets/sample/textures/block/stone.png.mcmeta', {'fusion': {'type': 'connecting', 'layout': 'full'}, 'animation': {}})
        with self.assertRaisesRegex(mod.ImportError, 'cannot contain animation'):
            self.compile('animated')

    def test_lootr_preserves_each_dusted_stage(self):
        simple(self.source)
        model = {'loader': 'lootr:brushable', 'opened': {'parent': 'sample:absent_player_model'}}
        for index in range(4):
            put(self.source, f'assets/sample/textures/block/stage_{index}.png', mod.encode_png(1, 1, bytes((index * 60, 0, 0, 255))))
            model[f'stage_{index}'] = {'parent': 'minecraft:block/cube_all', 'textures': {'all': f'sample:block/stage_{index}'}}
        put(self.source, 'assets/sample/models/block/brushable.json', model)
        put(self.source, 'assets/sample/blockstates/brushable.json', {'variants': {
            f'dusted={index}': {'model': 'sample:block/brushable'} for index in range(4)}})
        out, report = self.compile()
        text = (out / 'manifest.mwpack').read_text()
        for index in range(4):
            self.assertIn(f'state\tsample:brushable[dusted={index}]\t{index * 60:02x}0000\t255', text)
        self.assertTrue(any('player-opened' in warning for warning in report['warnings']))
        self.assertEqual(report['unsupported'], [])

    def test_packed_png_and_trailing_bytes(self):
        def packed(depth, color, width, row, extra=b''):
            return (b'\x89PNG\r\n\x1a\n' + mod.png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, 1, depth, color, 0, 0, 0))
                    + extra + mod.png_chunk(b'IDAT', zlib.compress(b'\0' + row)) + mod.png_chunk(b'IEND', b''))
        for depth, width, row, expected in ((1, 3, b'\xa0', [255, 0, 255]),
                                          (2, 3, b'\x1c', [0, 85, 255]),
                                          (4, 3, b'\x05\xf0', [0, 85, 255])):
            rgba = mod.decode_png(packed(depth, 0, width, row))[2]
            self.assertEqual(list(rgba[::4]), expected)
        palette = mod.png_chunk(b'PLTE', bytes((255, 0, 0, 0, 255, 0, 0, 0, 255)))
        rgba = mod.decode_png(packed(2, 3, 3, b'\x18', palette))[2]
        self.assertEqual(rgba, bytes((255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255)))
        original = mod.encode_png(1, 1, bytes((1, 2, 3, 255)))
        seen = []
        self.assertEqual(mod.decode_png(original + b'stale bytes', lambda: seen.append(True)), mod.decode_png(original))
        self.assertEqual(seen, [True])
        with self.assertRaisesRegex(mod.ImportError, 'trailing'):
            mod.decode_png(original + b'stale bytes')

    def test_lazy_assets_ignore_irrelevant_duplicate_metadata(self):
        simple(self.source)
        jar = self.root / 'metadata.jar'
        with zipfile.ZipFile(jar, 'w') as archive:
            archive.writestr('META-INF/LICENSE.txt', 'first')
            with self.assertWarns(UserWarning):
                archive.writestr('META-INF/LICENSE.txt', 'second')
            for path in self.source.rglob('*'):
                if path.is_file():
                    archive.write(path, path.relative_to(self.source).as_posix())
        assets = mod.Assets([jar])
        self.assertEqual(len(assets.archives), 0)
        self.assertNotIn('META-INF/LICENSE.txt', assets.files)
        self.assertEqual(assets.json('assets/sample/blockstates/stone.json')['variants']['']['model'], 'sample:block/stone')
        self.assertEqual(len(assets.archives), 1)
        assets.close()
        with zipfile.ZipFile(jar, 'a') as archive:
            with self.assertWarns(UserWarning):
                archive.writestr('assets/sample/blockstates/stone.json', '{}')
        with self.assertRaisesRegex(mod.ImportError, 'duplicate archive entry'):
            mod.Assets([jar])

    def test_rejected_models_do_not_consume_later_geometry_budget(self):
        simple(self.source)
        put(self.source, 'assets/sample/models/block/bad.json', {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': {}}] * 6})
        put(self.source, 'assets/sample/blockstates/aaa_bad.json', {'variants': {'': {'model': 'sample:block/bad'}}})
        limit = mod.MAX_COMPILED_ELEMENTS
        try:
            mod.MAX_COMPILED_ELEMENTS = 6
            _, report = self.compile()
        finally:
            mod.MAX_COMPILED_ELEMENTS = limit
        self.assertEqual(report['blocks'], 1)
        self.assertEqual(len(report['unsupported']), 1)

    def test_rejected_models_do_not_register_textures(self):
        simple(self.source)
        put(self.source, 'assets/sample/textures/block/unused.png', mod.encode_png(1, 1, bytes((1, 2, 3, 4))))
        put(self.source, 'assets/sample/models/block/bad.json', {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': {
            'down': {'texture': 'sample:block/unused'}, 'up': {'texture': 'sample:block/missing'}}}]})
        put(self.source, 'assets/sample/blockstates/aaa_bad.json', {'variants': {'': {'model': 'sample:block/bad'}}})
        out, report = self.compile()
        self.assertEqual(report['textures'], 1)
        self.assertNotIn('unused', (out / 'manifest.mwpack').read_text())
        self.assertEqual(len(list((out / 'textures').iterdir())), 1)

    def test_dimension_metadata_references_inline_and_incomplete(self):
        simple(self.source)
        put(self.source, 'data/sample/dimension/moon.json', {'type': 'sample:moon_type'})
        put(self.source, 'data/sample/dimension_type/moon_type.json', {'min_y': -64, 'height': 384, 'coordinate_scale': 2})
        put(self.source, 'data/sample/dimension/sky.json', {'type': {'min_y': 0, 'height': 256, 'coordinate_scale': 0.5}})
        put(self.source, 'data/sample/dimension/missing.json', {'type': 'unavailable:type'})
        put(self.source, 'data/sample/dimension/bad.json', {'type': {'min_y': 0, 'height': 255, 'coordinate_scale': 1}})
        out, report = self.compile()
        text = (out / 'manifest.mwpack').read_text()
        self.assertIn('dimension\tsample:moon\t-64\t384\t2', text)
        self.assertIn('dimension\tsample:sky\t0\t256\t0.5', text)
        self.assertEqual(report['dimensions'], 2)
        self.assertEqual(len(report['unsupported_dimensions']), 2)

    def test_dimension_only_pack(self):
        put(self.source, 'data/sample/dimension/moon.json', {'type': {'min_y': -2032, 'height': 4064, 'coordinate_scale': 0.000001}})
        out, report = self.compile()
        self.assertEqual(report['blocks'], 0)
        self.assertEqual(report['dimensions'], 1)
        self.assertEqual(list((out / 'textures').iterdir()), [])

    def test_cli_source_file_preserves_unicode_spaces_and_override_order(self):
        source = self.root / 'mod assets café'
        simple(source)
        override = self.root / 'resource override'
        put(override, 'assets/sample/textures/block/stone.png', mod.encode_png(1, 1, bytes((0, 255, 0, 255))))
        source_file = self.root / 'sources list.txt'
        source_file.write_bytes(('\ufeff\n' + str(source) + '\n\n').encode('utf-8'))
        output = self.root / 'cli output'
        run = subprocess.run([sys.executable, str(SCRIPT), '--sources-file', str(source_file), str(override), '-o', str(output)],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('state\tsample:stone\t00ff00\t255', (output / 'manifest.mwpack').read_text())
        source_file.write_bytes(bytes(1024 * 1024 + 1))
        oversized = subprocess.run([sys.executable, str(SCRIPT), '--sources-file', str(source_file), '-o', str(self.root / 'too big')],
                                   capture_output=True, text=True)
        self.assertNotEqual(oversized.returncode, 0)
        self.assertIn('exceeds 1 MiB', oversized.stderr)
        self.assertFalse((self.root / 'too big').exists())

    def test_cli_requires_at_least_one_source(self):
        run = subprocess.run([sys.executable, str(SCRIPT), '-o', str(self.root / 'empty')], capture_output=True, text=True)
        self.assertEqual(run.returncode, 2)
        self.assertIn('provide a source path', run.stderr)
        self.assertFalse((self.root / 'empty').exists())

    def test_verified_builtin_adapters_and_invisible_identity(self):
        adapter_assets(self.source)
        out, report = self.compile()
        text = (out / 'manifest.mwpack').read_text()
        self.assertIn('state\tae2:matrix_frame\t000000\t0\ninvisible\nendstate', text)
        self.assertIn('element\t0\t0\t0\t12\t10\t16', text)
        self.assertEqual(report['blocks'], 6)
        self.assertEqual(report['unsupported'], [])
        self.assertEqual(report['unsupported_states'][0]['state'], 'ae2:drive[facing=north,spin=1]')
        self.assertTrue(any('empty-drive' in w for w in report['warnings']))
        self.assertTrue(any('unpowered' in w for w in report['warnings']))
        textures = {f[2]: out / f[3] for line in text.splitlines() if (f := line.split('\t'))[0] == 'texture'}
        _, _, pixels = mod.decode_png(textures['mineways:ae2/quartz_glass_isolated'].read_bytes())
        self.assertEqual(pixels[:4], bytes((255, 0, 0, 255)))
        self.assertEqual(pixels[20:24], bytes((0, 0, 255, 128)))
        _, _, pixels = mod.decode_png(textures['mineways:ae2/spatial_pylon_unlinked'].read_bytes())
        self.assertEqual(pixels[:4], bytes((63, 63, 191, 255)))
        self.assertEqual(pixels[4:8], bytes((128, 128, 128, 255)))
        # A supplied standard override must take precedence over the curated adapter.
        put(self.source, 'assets/ae2/models/block/matrix_frame.json', {'parent': 'minecraft:block/cube_all', 'textures': {'all': 'ae2:block/drive'}})
        override, _ = self.compile('override')
        state = (override / 'manifest.mwpack').read_text().split('state\tae2:matrix_frame\t', 1)[1].split('endstate', 1)[0]
        self.assertIn('usemodel', state)
        self.assertNotIn('invisible', state)

    def test_fluid_levels_use_verified_textures_and_heights(self):
        adapter_assets(self.source)
        compiler = mod.Compiler(mod.Assets([self.source]))
        states = compiler.block('allthemodium:molten_allthemodium_block',
                                compiler.assets.json('assets/allthemodium/blockstates/molten_allthemodium_block.json'))
        by_name = {state[0]: state[3][0]['elements'][0] for state in states}
        for level in (0, 7, 8, 15):
            element = by_name[f'allthemodium:molten_allthemodium_block[level={level}]']
            self.assertAlmostEqual(element['coordinates'][4], 16 / 9 if level == 7 else 128 / 9)
            faces = {face[0]: face for face in element['faces']}
            self.assertEqual(faces['up'][-2], 'allthemodium:block/fluid/atm_molten_still')
            self.assertEqual(faces['north'][-2], 'allthemodium:block/fluid/atm_molten_flow')
            self.assertEqual(faces['north'][3:5], (8, 8))
        self.assertEqual(len(states), 17)
        compiler.assets.close()

    def test_ae2_z_rotations_match_existing_axes_or_report_unsupported(self):
        # Independent right-handed negative quarter turns from the verified JOML call.
        self.assertEqual(mod.quarter_turn_orientation(90, 0), ((1, 0, 0), (0, 0, -1), (0, 1, 0)))
        self.assertEqual(mod.quarter_turn_orientation(0, 90), ((0, 0, 1), (0, 1, 0), (-1, 0, 0)))
        self.assertEqual(mod.quarter_turn_orientation(0, 0, 90), ((0, -1, 0), (1, 0, 0), (0, 0, 1)))
        self.assertEqual(mod.quarter_turn_orientation(90, 90, 90), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)))
        simple(self.source)
        compiler = mod.Compiler(mod.Assets([self.source]))
        supported = unsupported = 0
        for x in (0, 90, 180, 270):
            for y in (0, 90, 180, 270):
                for z in (0, 90, 180, 270):
                    try:
                        result = compiler.compile_model({'model': 'sample:block/stone', 'x': x, 'y': y, 'ae2:z': z})
                    except mod.ImportError as error:
                        self.assertIn('AE2 Z rotation', str(error))
                        unsupported += 1
                    else:
                        a, b, _ = result['rotation']
                        self.assertEqual(mod.quarter_turn_orientation(a, b), mod.quarter_turn_orientation(x, y, z))
                        supported += 1
        self.assertEqual((supported, unsupported), (48, 16))
        self.assertEqual(compiler.compile_model({'model': 'sample:block/stone', 'ae2:z': 180})['rotation'][:2], (180, 180))
        compiler.assets.close()

    def test_standard_fluid_resource_override_takes_precedence(self):
        adapter_assets(self.source)
        put(self.source, 'assets/allthemodium/models/block/fluid/molten.json',
            {'parent': 'minecraft:block/cube_all', 'textures': {'all': 'allthemodium:block/fluid/atm_molten_still'}})
        compiler = mod.Compiler(mod.Assets([self.source]))
        states = compiler.block('allthemodium:molten_allthemodium_block',
                                compiler.assets.json('assets/allthemodium/blockstates/molten_allthemodium_block.json'))
        self.assertEqual(len(states), 1)
        self.assertEqual(states[0][3][0]['elements'][0]['coordinates'][4], 16)
        self.assertFalse(any('flat fluid' in warning for warning in compiler.warnings))
        compiler.assets.close()

    def test_output_is_never_overwritten(self):
        simple(self.source)
        out, _ = self.compile()
        sentinel = out / 'manifest.mwpack'
        before = sentinel.read_bytes()
        with self.assertRaisesRegex(mod.ImportError, 'already exists'):
            self.compile()
        self.assertEqual(sentinel.read_bytes(), before)

    def test_png_filters_palette_and_grayscale(self):
        first = bytes((10, 20, 30, 40, 50, 60))
        expected = bytes((10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255))
        def png(color, raw, extra=b''):
            return (b'\x89PNG\r\n\x1a\n' + mod.png_chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 2, 8, color, 0, 0, 0))
                    + extra + mod.png_chunk(b'IDAT', zlib.compress(raw)) + mod.png_chunk(b'IEND', b''))
        for filter_type, second in ((1, bytes((70, 80, 90, 30, 30, 30))),
                                    (2, bytes((60, 60, 60, 60, 60, 60))),
                                    (3, bytes((65, 70, 75, 45, 45, 45))),
                                    (4, bytes((60, 60, 60, 30, 30, 30)))):
            self.assertEqual(mod.decode_png(png(2, b'\0' + first + bytes((filter_type,)) + second))[2], expected)
        palette = mod.png_chunk(b'PLTE', bytes((1, 2, 3, 4, 5, 6))) + mod.png_chunk(b'tRNS', bytes((0, 128)))
        self.assertEqual(mod.decode_png(png(3, bytes((0, 0, 1, 0, 1, 0)), palette))[2],
                         bytes((1, 2, 3, 0, 4, 5, 6, 128, 4, 5, 6, 128, 1, 2, 3, 0)))
        self.assertEqual(mod.decode_png(png(4, bytes((0, 10, 20, 30, 40, 0, 50, 60, 70, 80))))[2],
                         bytes((10, 10, 10, 20, 30, 30, 30, 40, 50, 50, 50, 60, 70, 70, 70, 80)))

    def test_png_limits_animation_and_bad_crc(self):
        png = mod.encode_png(1, 2, bytes((255, 0, 0, 255, 0, 255, 0, 255)))
        self.assertEqual(mod.decode_png(png), (1, 2, bytes((255, 0, 0, 255, 0, 255, 0, 255))))
        simple(self.source)
        put(self.source, 'assets/sample/textures/block/stone.png', png)
        out, report = self.compile()
        self.assertTrue(any('first frame' in w for w in report['warnings']))
        self.assertEqual(mod.decode_png((out / 'textures/0000.png').read_bytes())[0:2], (1, 1))
        bad = bytearray(png)
        bad[-1] ^= 1
        with self.assertRaisesRegex(mod.ImportError, 'checksum'):
            mod.decode_png(bad)
        bomb = (b'\x89PNG\r\n\x1a\n' + mod.png_chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
                + mod.png_chunk(b'IDAT', zlib.compress(bytes(10000))) + mod.png_chunk(b'IEND', b''))
        with self.assertRaisesRegex(mod.ImportError, 'size mismatch'):
            mod.decode_png(bomb)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--fixture':
        print(build_fixture(sys.argv[2]))
    else:
        unittest.main()
