"""Exact recovery of unused UVs on zero-area model faces."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, simple, put, uv_assets


class ZeroAreaUvTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mineways-uv-')
        self.root = Path(self.temporary.name)
        simple(self.root)
        self.assets = mod.Assets([self.root])
        self.compiler = mod.Compiler(self.assets)

    def tearDown(self):
        self.assets.close()
        self.temporary.cleanup()

    def face(self, **changes):
        return {'texture': 'sample:block/stone', 'uv': [0, 0, 16, 16], **changes}

    def test_zero_area_faces_skip_invalid_uvs_and_missing_textures_on_each_axis(self):
        directions = (('west', 'east'), ('down', 'up'), ('north', 'south'))
        for flat_axis, visible in enumerate(directions):
            with self.subTest(axis=flat_axis):
                start, end = [0, 0, 0], [16, 16, 16]
                start[flat_axis] = end[flat_axis] = 8
                faces = {side: self.face() if side in visible else
                         self.face(texture='absent:unused', uv=[-5, 0, 30, 18])
                         for side in mod.DIRECTIONS}
                model = {'elements': [{'from': start, 'to': end, 'faces': faces,
                                      'rotation': {'axis': 'y', 'angle': 22.5, 'origin': [8, 8, 8]}}]}
                result = self.compiler.compile_model({'model': model})
                self.assertEqual({face[0] for face in result['elements'][0]['faces']}, set(visible))
        self.assertEqual(set(self.compiler.textures), {'sample:block/stone'})
        self.assertFalse(self.compiler.warnings)

    def test_visible_faces_still_reject_invalid_uvs_and_missing_textures(self):
        for bad, reason in [(self.face(uv=[0, 0, 257, 16]), 'number outside -256..256'),
                            (self.face(texture='absent:visible'), 'missing texture')]:
            with self.subTest(reason=reason), self.assertRaisesRegex(mod.ImportError, reason):
                self.compiler.compile_model({'model': {'elements': [
                    {'from': [0, 8, 0], 'to': [16, 8, 16], 'faces': {'up': bad}}]}})

    def test_empty_elements_are_removed_and_do_not_consume_budget(self):
        empty = {'from': [0, 0, 0], 'to': [0, 16, 0],
                 'faces': {'north': self.face(texture='absent:unused', uv=[-5, 0, 30, 18])}}
        visible = {'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': {'north': self.face()}}
        before = mod.MAX_COMPILED_ELEMENTS
        try:
            mod.MAX_COMPILED_ELEMENTS = 1
            result = self.compiler.compile_model({'model': {'elements': [empty, visible]}})
            self.assertEqual(len(result['elements']), 1)
            self.assertEqual(self.compiler.element_count, 1)
        finally:
            mod.MAX_COMPILED_ELEMENTS = before

    def test_fully_empty_model_is_not_marked_invisible(self):
        model = {'elements': [{'from': [8, 8, 8], 'to': [8, 8, 8],
                               'faces': {'north': self.face(texture='absent:unused')}}]}
        with self.assertRaisesRegex(mod.ImportError, 'no nonzero-area faces'):
            self.compiler.compile_model({'model': model})
        self.assertEqual(self.compiler.element_count, 0)
        self.assertFalse(self.compiler.textures)

    def test_inverted_nonzero_area_geometry_remains_visible(self):
        model = {'elements': [{'from': [16, 16, 16], 'to': [0, 0, 0],
                               'faces': {'north': self.face()}}]}
        result = self.compiler.compile_model({'model': model})
        self.assertEqual(result['elements'][0]['coordinates'][:6], (16, 16, 16, 0, 0, 0))
        self.assertEqual(len(result['elements'][0]['faces']), 1)


class ExpandedUvTests(unittest.TestCase):
    setUp = ZeroAreaUvTests.setUp
    tearDown = ZeroAreaUvTests.tearDown
    face = ZeroAreaUvTests.face

    def test_expanded_values_preserved_and_reported_without_derived_textures(self):
        for uv in ([-8, -8, 24, 24], [24, 24, -8, -8], [24, -8, 24, 24]):
            model = {'elements': [{'from': [2, 3, 4], 'to': [14, 13, 12],
                                   'faces': {'up': self.face(uv=uv, rotation=90)}}]}
            result = self.compiler.compile_model({'model': model, 'x': 0, 'y': 0, 'ae2:z': 90, 'uvlock': True})
            self.assertEqual(result['elements'][0]['faces'][0][1:5], tuple(uv))
            self.assertEqual(result['rotation'], (0, 0, 1, 90))
        self.assertEqual(set(self.compiler.textures), {'sample:block/stone'})
        self.assertTrue(any('approximates Minecraft atlas extrapolation' in warning for warning in self.compiler.warnings))

    def test_subdivision_budget_constant_axes_and_bounds(self):
        self.assertTrue(mod.expanded_uv([(-128, -128), (128, 128)]))  # exactly256cells
        self.assertTrue(mod.expanded_uv([(-256, 24), (256, 24)]))  # constantV costsonecell
        self.assertTrue(mod.expanded_uv([(256, 256), (256, 256)]))
        for points in ([(-128, -128), (128.001, 128)], [(-256, -256), (256, 256)],
                       [(0, 0), (257, 0)], [(0, 0), (float('inf'), 0)], [(0, 0), (10**400, 0)]):
            with self.subTest(points=points), self.assertRaises(mod.ImportError):
                mod.expanded_uv(points)

    def test_v5_fixture_preserves_pixels_and_old_formats(self):
        ordinary = self.root / 'ordinary'
        report = mod.compile_pack([self.root], ordinary)
        self.assertEqual(report['format'], 2)
        uv_assets(self.root)
        output = self.root / 'expanded'
        report = mod.compile_pack([self.root], output)
        self.assertEqual(report['format'], 5)
        manifest = (output / 'manifest.mwpack').read_text()
        self.assertTrue(manifest.startswith('MINEWAYS_MOD_PACK\t5\n'))
        self.assertIn('\t-8\t-8\t24\t24\t', manifest)
        self.assertEqual(report['textures'], 2)
        for line in manifest.splitlines():
            if line.startswith('texture\t') and '\tfixture:block/obj_pattern\t' in line:
                self.assertEqual((output / line.split('\t')[3]).read_bytes(),
                                 (self.root / 'assets/fixture/textures/block/obj_pattern.png').read_bytes())
        c = mod.Compiler(mod.Assets([self.root]))
        self.addCleanup(c.assets.close)
        polygon = c.compile_model({'model': 'fixture:block/uv_polygon'})['polygons'][0]
        self.assertEqual([v[3:] for v in polygon['vertices']], [(-8., 24.), (24., 24.), (8., -8.)])

    def test_failed_expanded_state_does_not_force_v5_or_retain_warning(self):
        put(self.root, 'assets/sample/blockstates/partial.json', {'variants': {
            'valid=false': {'model': {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16],
                'faces': {'down': self.face(uv=[-8, 0, 24, 16]), 'up': self.face(texture='absent:fail')}}]}},
            'valid=true': {'model': 'sample:block/stone'}}})
        report = mod.compile_pack([self.root], self.root / 'pack')
        self.assertEqual(report['format'], 2)
        self.assertFalse(any('atlas extrapolation' in warning for warning in report['warnings']))

    def test_obj_flip_bounds_apply_to_v_only(self):
        uv_assets(self.root)
        path = self.root / 'assets/fixture/models/block/uv_polygon.obj'
        original = path.read_text()
        for flip, uv, expected in [(True, '-16 17', (-256., -256.)),
                                   (False, '-16 16', (-256., 256.))]:
            path.write_text('\n'.join('vt ' + uv if line.startswith('vt ') else line
                                      for line in original.splitlines()) + '\n')
            c = mod.Compiler(mod.Assets([self.root]))
            self.addCleanup(c.assets.close)
            result = c.compile_model({'model': {'parent': 'fixture:block/uv_polygon', 'flip_v': flip}})
            self.assertTrue(all(vertex[3:] == expected for vertex in result['polygons'][0]['vertices']))

    def test_obj_expanded_budget_matches_standard_faces(self):
        uv_assets(self.root)
        path = self.root / 'assets/fixture/models/block/uv_polygon.obj'
        original = path.read_text()
        path.write_text(original.replace('vt -0.5 -0.5', 'vt -8 -7').replace('vt 1.5 -0.5', 'vt 8.1 -7').replace('vt 0.5 1.5', 'vt 0 9'))
        c = mod.Compiler(mod.Assets([self.root]))
        self.addCleanup(c.assets.close)
        with self.assertRaisesRegex(mod.ImportError, '256 texture cells'):
            c.compile_model({'model': 'fixture:block/uv_polygon'})


if __name__ == '__main__':
    unittest.main()
