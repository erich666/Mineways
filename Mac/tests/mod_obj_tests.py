"""Original OBJ fixtures and bounded-parser regressions; no mod files required."""
import json
from pathlib import Path
import tempfile
import unittest
import mod_assets as assets_test

mod = assets_test.mod


obj_assets = assets_test.obj_assets

class ObjTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        obj_assets(self.root)

    def tearDown(self):
        self.temp.cleanup()

    def compiler(self):
        return mod.Compiler(mod.Assets([self.root]))

    def test_triangle_quad_rotation_uv_and_material(self):
        c = self.compiler()
        output = c.block('fixture:obj_mesh', c.assets.json('assets/fixture/blockstates/obj_mesh.json'))
        self.assertEqual(len(output), 2)
        model = output[0][3][0]
        self.assertEqual([len(p['vertices']) for p in model['polygons']], [3, 4])
        self.assertEqual(model['polygons'][0]['vertices'][0], (-4., 0., 0., 2., 12.))
        self.assertEqual(output[1][3][0]['rotation'], (90, 90, 0))
        texture = model['polygons'][0]['texture']
        pixels = mod.decode_png(c.textures[texture][0])[2]
        self.assertEqual(pixels[(3*16+5)*4:(3*16+5)*4+4], bytes((40, 48, 32, 255)))
        self.assertTrue(any('flat face normals' in w for w in c.warnings))
        destination = self.root / 'pack'
        report = mod.compile_pack([self.root], destination)
        self.assertEqual(report['format'], 3)
        manifest = (destination / 'manifest.mwpack').read_text()
        self.assertTrue(manifest.startswith('MINEWAYS_MOD_PACK\t3\n'))
        self.assertEqual(manifest.count('\npolygon\t'), 2) # shared by both rotated states

    def test_reject_bad_indices_paths_numbers_and_records(self):
        path = self.root / 'assets/fixture/models/block/obj_mesh.obj'
        original = path.read_text()
        bad = ['f 0/1 2/2 3/3', 'f 99/1 2/2 3/3', 'f -999/1 2/2 3/3',
               'f 1//1 2//1 3//1', 'f 1/1/99 2/2/1 3/3/1',
               'f 1/1 2/2 3/3 4/4 5/5', 'v nan 0 0', 'v 16.01 0 0',
               'vt 17.1 0', 'vc 1 0 0', 'mtllib ../escape.mtl', 'mtllib /etc/passwd',
               'mtllib file:///tmp/leak.mtl']
        for record in bad:
            with self.subTest(record=record):
                path.write_text(original + record + '\n')
                with self.assertRaises(mod.ImportError):
                    self.compiler().compile_model({'model': 'fixture:block/obj_mesh'})
        path.write_text(original)
        mtl = self.root / 'assets/fixture/models/block/obj_mesh.mtl'
        for ref in ('../../secret', '/tmp/secret.png', 'https://example.com/a.png', 'C:/private.png'):
            mtl.write_text('newmtl triangle\nmap_Kd ' + ref + '\n')
            with self.assertRaises(mod.ImportError):
                self.compiler().compile_model({'model': 'fixture:block/obj_mesh'})

    def test_override_types_tint_alpha_and_work_budgets(self):
        c = self.compiler()
        model = c.model('fixture:block/obj_mesh')
        for value in (True, 1, [], ['x'], False, None, ''):
            with self.subTest(override=value), self.assertRaises(mod.ImportError):
                c.compile_model({'model': {**model, 'mtl_override': value}})
        texture = self.root / 'assets/fixture/textures/block/obj_pattern.png'
        texture.write_bytes(mod.encode_png(2, 2, bytes((255, 0, 0, 0, 0, 0, 255, 255)) * 2))
        c = self.compiler()
        result = c.compile_model({'model': 'fixture:block/obj_mesh'})
        self.assertEqual(c.textures[result['polygons'][0]['texture']][1], [0, 0, 63])
        path = self.root / 'assets/fixture/models/block/obj_mesh.obj'
        path.write_text(path.read_text() + 'mtllib obj_mesh.mtl\n' * 100)
        c = self.compiler()
        reads = []
        original_read = c.assets.read
        c.assets.read = lambda path: (reads.append(path), original_read(path))[1]
        c.compile_model({'model': 'fixture:block/obj_mesh'})
        self.assertEqual(reads.count('assets/fixture/models/block/obj_mesh.mtl'), 1)
        for setting in ('MAX_LIBRARY_BYTES', 'MAX_GENERATED_PIXELS'):
            old = getattr(mod._obj_module, setting)
            try:
                setattr(mod._obj_module, setting, 1)
                with self.assertRaises(mod.ImportError):
                    self.compiler().compile_model({'model': 'fixture:block/obj_mesh'})
            finally:
                setattr(mod._obj_module, setting, old)

    def test_static_visibility_inheritance_and_transforms(self):
        path = self.root / 'assets/fixture/models/block/obj_mesh.obj'
        path.write_text(path.read_text().replace('usemtl triangle', 'o Triangle\nusemtl triangle').replace('usemtl quad', 'o Quad\nusemtl quad'))
        model = self.root / 'assets/fixture/models/block/obj_mesh.json'
        own = json.loads(model.read_text())
        own['visibility'] = {'Triangle': False, 'Quad': False}
        model.write_text(json.dumps(own))
        c = self.compiler()
        result = c.compile_model({'model': {'parent': 'fixture:block/obj_mesh', 'visibility': {'Triangle': True},
            'transform': {'origin': 'corner', 'rotation': [0, 0, 90], 'translation': [0, .25, 0]}}})
        self.assertEqual(len(result['polygons']), 1)
        # Root origin0 is followed by ObjModel.blockCenterToCorner: rotation about(.5,.5,.5).
        self.assertEqual(result['polygons'][0]['vertices'][0][:3], (16., 0., 0.))
        self.assertTrue(c.compile_model({'model': 'fixture:block/obj_mesh'})['invisible'])
        result = c.compile_model({'model': {'loader': 'neoforge:composite',
            'visibility': {'hidden': False}, 'transform': {'translation': [0, 1, 0]},
            'children': {'hidden': {'parent': 'missing:model'},
                         'visible': {'parent': 'fixture:block/obj_mesh', 'visibility': {'Quad': True}}}}})
        self.assertEqual(len(result['polygons']), 1)
        self.assertEqual(result['polygons'][0]['vertices'][0][:3], (0., 16., 16.))
        hidden = {'loader': 'neoforge:composite', 'visibility': {'masked': False},
                  'children': {'masked': {'parent': 'fixture:block/obj_mesh'}}}
        self.assertTrue(c.compile_model({'model': hidden})['invisible'])
        self.assertTrue(c.compile_model({'model': {'loader': 'neoforge:composite',
            'children': {'masked': {'parent': 'fixture:block/obj_mesh'}}}})['invisible'])
        output = c.block('fixture:hidden_composite', {'variants': {'': {'model': hidden}}})
        self.assertEqual(output[0][3], [])
        self.assertEqual(output[0][2], 0)

        for axis in ('', 'xy', 'w', 0):
            with self.assertRaises(mod.ImportError):
                mod._obj_module.model_transform({'rotation': {axis: 90}}, mod.ImportError)
        transform = mod._obj_module.model_transform({'rotation': [{'x': 90}, {'z': 90}], 'origin': 'corner'}, mod.ImportError)
        self.assertEqual(transform((0., 0., 0.)), (16., 16., 0.))
        for spec in ({'translation': [100, 0, 0]}, {'scale': 0}, {'origin': True}, {'rotation': [0, 0, float('nan')]}, {'scale': [64, 64, 64]}):
            with self.assertRaises(mod.ImportError):
                c.compile_model({'model': {**own, 'visibility': {'Triangle': True}, 'transform': spec}})

    def test_huge_transform_numbers_and_empty_obj(self):
        for spec in ({'rotation': [{'x': 0}, [{'z': 1}] * 64]},
                     {'rotation': [{'x': 0}] * 65},
                     {'translation': [10**400, 0, 0]}, {'rotation': {'x': 10**400}},
                     {'scale': 10**400}, {'origin': [10**400, 0, 0]}):
            with self.assertRaises(mod.ImportError):
                mod._obj_module.model_transform(spec, mod.ImportError)
        path = self.root / 'assets/fixture/models/block/obj_mesh.obj'
        path.write_text('o nonexistent\n')
        c = self.compiler()
        for visibility in ({'nonexistent': True}, {'nonexistent': False}):
            with self.assertRaises(mod.ImportError):
                c.compile_model({'model': {**c.model('fixture:block/obj_mesh'), 'visibility': visibility}})

    def test_bounds_and_degenerate_faces(self):
        c = self.compiler()
        model = c.model('fixture:block/obj_mesh')
        for key, value in (('flip_v', 1), ('transform', {'bogus': 1}), ('visibility', {'part': 1})):
            with self.assertRaises(mod.ImportError):
                c.compile_model({'model': {**model, key: value}})
        path = self.root / 'assets/fixture/models/block/obj_mesh.obj'
        path.write_text(path.read_text() + 'f 1/1 1/1 1/1\n')
        c = self.compiler()
        result = c.compile_model({'model': 'fixture:block/obj_mesh'})
        self.assertEqual(len(result['polygons']), 2)
        self.assertTrue(any('degenerate OBJ' in w for w in c.warnings))
        original = mod._obj_module.MAX_POLYGONS
        try:
            mod._obj_module.MAX_POLYGONS = 1
            with self.assertRaises(mod.ImportError):
                self.compiler().compile_model({'model': 'fixture:block/obj_mesh'})
        finally:
            mod._obj_module.MAX_POLYGONS = original


if __name__ == '__main__':
    unittest.main()
