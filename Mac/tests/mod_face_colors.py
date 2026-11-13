"""NeoForge static ARGB attributes use face-or-element metadata, not key merging."""
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import mod_assets as fixtures

mod = fixtures.mod
OMITTED = object()

class FaceColorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.pixels = bytes((255,0,0,0, 0,0,255,255, 100,200,50,128, 255,255,255,64))
        fixtures.put(self.root,'assets/fixture/textures/pattern.png',mod.encode_png(2,2,self.pixels))
        fixtures.put(self.root,'assets/fixture/blockstates/unused.json',{'variants':{}})
        self.compiler = mod.Compiler(mod.Assets([self.root]))
        self.texture = self.compiler.texture('fixture:pattern')

    def tearDown(self):
        self.compiler.assets.close()
        self.temp.cleanup()

    def color(self, data, face=OMITTED):
        return self.compiler.face_color_texture(self.texture, {'neoforge_data':data}, {} if face is OMITTED else {'neoforge_data':face}, 'fixture:model')

    def test_argb_multiplies_rgba_and_alpha_weights_map_average(self):
        name = self.color({'color':'804080ff'})
        png, rgb, alpha = self.compiler.textures[name]
        self.assertEqual(mod.decode_png(png)[2], bytes((64,0,0,0, 0,0,255,128, 25,100,50,64, 64,128,255,32)))
        self.assertEqual((rgb,alpha),([16,46,196],56))
        self.assertEqual(self.color({'color':0x804080ff}), name)
        self.assertEqual(self.color({'color':0x804080ff - 2**32}), name)
        self.assertEqual(self.compiler.face_texture_pixel_count,4)
        self.assertEqual(self.compiler.textures[self.texture][0],mod.encode_png(2,2,self.pixels))
        zero = self.color({'color':'0'})
        self.assertEqual(self.compiler.textures[zero][1:],([0,0,0],0))

    def test_face_object_replaces_all_element_values(self):
        parent={'color':'ffff0000','block_light':15,'ambient_occlusion':False}
        self.assertNotEqual(self.color(parent),self.texture)
        self.compiler.warnings.clear()
        self.assertEqual(self.color(parent,{}),self.texture)
        self.assertFalse(self.compiler.warnings)
        green=self.color(parent,{'color':'ff00ff00'})
        self.assertEqual(mod.decode_png(self.compiler.textures[green][0])[2][8:12],bytes((0,200,0,128)))
        self.assertFalse(self.compiler.warnings)

    def test_hex_strings_and_numeric_codec_truncation(self):
        # String "255" means hexadecimal0x255; numeric255 means0xff.
        self.assertNotEqual(self.color({'color':'255'}),self.color({'color':255}))
        self.assertEqual(self.color({'color':255.9}),self.color({'color':255}))
        self.assertEqual(self.color({'color':-1.9}),self.texture)
        for white in ('ffffffff','-1','7fffffffffffffff'):
            self.assertEqual(self.color({'color':white}),self.texture)
        self.color({'block_light':15.9,'sky_light':-.5,'ambient_occlusion':False})
        self.assertTrue(any('material emission' in w for w in self.compiler.warnings))
        self.assertTrue(any('ambient-occlusion' in w for w in self.compiler.warnings))

    def test_malformed_metadata_rejected_before_partial_override(self):
        invalid=[None,[],{'color':True},{'color':{}},{'color':'0xff00ff00'}, {'color':'ffffffffffffffff'},
                 {'color':'f'*10000},{'color':10**400},{'color':float('inf')},{'color':float('nan')},
                 {'block_light':16},{'sky_light':-1},{'block_light':True},{'ambient_occlusion':0}]
        for data in invalid:
            with self.subTest(data=str(data)[:60]):
                with self.assertRaises(mod.ImportError):self.color(data,{})
                with self.assertRaises(mod.ImportError):self.color({},data)

    def test_legacy_hint_warns_without_dropping_existing_model(self):
        self.assertEqual(self.compiler.face_color_texture(self.texture,{'forge_data':{'color':'ff00ff00'}},{},'fixture:legacy'),self.texture)
        self.assertTrue(any('legacy forge_data' in w for w in self.compiler.warnings))

    def test_budget_cache_and_rollback_remain_consistent(self):
        with patch.object(mod._obj_module,'MAX_GENERATED_PIXELS',4):
            name=self.color({'color':'ff00ff00'})
            self.assertEqual(self.color({'color':'ff00ff00'}),name)
            with self.assertRaisesRegex(mod.ImportError,'pixel budget'):self.color({'color':'ffff0000'})
            self.compiler.remove_last_texture()
            self.assertEqual(self.compiler.face_texture_pixel_count,0)
            self.assertEqual(self.compiler.face_texture_pixels,{})
            self.assertNotEqual(self.color({'color':'ffff0000'}),name)

    def test_compiled_face_hook_and_failed_state_rollback(self):
        element={'from':[0,0,0],'to':[16,16,16], 'neoforge_data':{'color':'804080ff'},
                 'faces':{'up':{'texture':'fixture:pattern'},'north':{'texture':'fixture:pattern','neoforge_data':{}}}}
        model={'elements':[element]}
        compiled=self.compiler.compile_model({'model':model})
        faces={f[0]:f[-2] for f in compiled['elements'][0]['faces']}
        self.assertEqual(faces['north'],self.texture)
        self.assertNotEqual(faces['up'],self.texture)
        self.compiler.remove_last_texture()
        bad=copy.deepcopy(model);bad['elements'][0]['faces']['up']['uv']=[0,0,1000,1000]
        with self.assertRaises(mod.ImportError):self.compiler.block('fixture:bad',{'variants':{'':{'model':bad}}})
        self.assertEqual(self.compiler.face_texture_pixel_count,0)
        self.assertEqual(self.compiler.face_texture_pixels,{})
        with patch.object(mod._obj_module,'MAX_GENERATED_PIXELS',4):
            self.compiler.compile_model({'model':model})
        self.assertEqual(self.compiler.face_texture_pixel_count,4)

if __name__ == '__main__':unittest.main()
