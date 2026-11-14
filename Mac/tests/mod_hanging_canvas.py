"""Hanging source planes, attached defaults, partial palettes, and shared alpha cache."""
import math
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod,put
from mod_hanging_canvas_fixtures import hanging_canvas_sign_assets

canvas=mod._canvas_sign_module


class HangingCanvas(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        hanging_canvas_sign_assets(self.root,mod.encode_png)

    def compiler(self):
        assets=mod.Assets([self.root]);self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self,compiler=None,block='farmersdelight:hanging_canvas_sign',state=None,**turn):
        return (compiler or self.compiler()).compile_model({'model':canvas.MODEL,**turn},selector=state,block_name=block)

    def test_literal_board_chain_faces_and_uvs(self):
        result=self.compile();faces=result['polygons']
        self.assertEqual(len(faces),14)
        self.assertEqual(faces[0]['vertices'],[(15,10,7,4,6),(1,10,7,.5,6),(1,10,9,.5,7),(15,10,9,4,7)])
        # First chain is source-local -float(pi/4), offset(-5,-6,0).
        angle=0.7853981852531433
        cx=1.5*math.cos(angle);cz=1.5*math.sin(angle)
        expected=[(3+cx,16,8-cz,.75,3),(3-cx,16,8+cz,0,3),
                  (3-cx,10,8+cz,0,6),(3+cx,10,8-cz,.75,6)]
        for actual,want in zip(faces[6]['vertices'],expected):
            for a,b in zip(actual,want):self.assertAlmostEqual(a,b)
        self.assertEqual([v[3:] for v in faces[7]['vertices']],[(1.5,3),(.75,3),(.75,6),(1.5,6)])
        self.assertEqual([v[3:] for v in faces[8]['vertices']],[(2.25,3),(1.5,3),(1.5,6),(2.25,6)])
        # Two visible sides per chain, no zero-area top/side strips.
        for index in range(6,14,2):
            pair=faces[index:index+2];normals=[]
            for face in pair:
                a,b,c=(v[:3] for v in face['vertices'][:3])
                ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
                n=[ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)]
                self.assertAlmostEqual(math.sqrt(sum(v*v for v in n)),18)
                normals.append(n)
            self.assertLess(sum(a*b for a,b in zip(*normals)),0)
            self.assertEqual({v[:3] for v in pair[0]['vertices']},{v[:3] for v in pair[1]['vertices']})

    def test_attached_plate_and_wall_plank_exact_visibility(self):
        attached=self.compile(state={'attached':'true'})['polygons']
        self.assertEqual(len(attached),8)
        self.assertEqual(attached[6]['vertices'],[(14,16,8,6.5,3),(2,16,8,3.5,3),(2,10,8,3.5,6),(14,10,8,6.5,6)])
        self.assertEqual([v[3:] for v in attached[7]['vertices']],[(9.5,3),(6.5,3),(6.5,6),(9.5,6)])
        wall=self.compile(block='farmersdelight:wall_hanging_canvas_sign',state={'facing':'south'})['polygons']
        self.assertEqual(len(wall),20)
        points=[v[:3] for p in wall[6:12] for v in p['vertices']]
        self.assertEqual(tuple(min(p[i] for p in points) for i in range(3)),(0,14,6))
        self.assertEqual(tuple(max(p[i] for p in points) for i in range(3)),(16,16,10))
        self.assertEqual(wall[:6],attached[:6])
        self.assertEqual(wall[12:],self.compile()['polygons'][6:])
        self.assertEqual(self.compile(),self.compile(state={'attached':'false','rotation':'0'}))
        block='farmersdelight:wall_hanging_canvas_sign'
        self.assertEqual(self.compile(block=block),self.compile(block=block,state={'facing':'north'}))

    def test_sixteen_angles_and_four_wall_facings_preserve_uv(self):
        for attached in ('false','true'):
            original=self.compile(state={'attached':attached})['polygons']
            for rotation in range(16):
                actual=self.compile(state={'attached':attached,'rotation':str(rotation)})['polygons']
                theta=math.radians(rotation*22.5)
                for p,q in zip(original,actual):
                    for a,b in zip(p['vertices'],q['vertices']):
                        x,z=a[0]-8,a[2]-8
                        self.assertAlmostEqual(b[0],8+math.cos(theta)*x-math.sin(theta)*z)
                        self.assertAlmostEqual(b[2],8+math.sin(theta)*x+math.cos(theta)*z)
                        self.assertEqual(a[1],b[1]);self.assertEqual(a[3:],b[3:])
        block='farmersdelight:wall_hanging_canvas_sign'
        for facing,theta in (('north',math.pi),('east',3*math.pi/2),('south',0),('west',math.pi/2)):
            first=self.compile(block=block,state={'facing':facing})['polygons'][0]['vertices'][0]
            self.assertAlmostEqual(first[0],8+7*math.cos(theta)+math.sin(theta))
            self.assertAlmostEqual(first[2],8+7*math.sin(theta)-math.cos(theta))

    def test_partial_expansion_and_nested_guards(self):
        compiler=self.compiler();application={'model':canvas.MODEL}
        block='farmersdelight:hanging_canvas_sign'
        rows=compiler.block(block,{'variants':{'':application}})
        self.assertEqual(len(rows),51)
        bystate={r[0]:r[3][0] for r in rows}
        self.assertEqual(len(bystate[block+'[attached=true]']['polygons']),8)
        self.assertEqual(len(bystate[block+'[rotation=7]']['polygons']),14)
        self.assertIn(block+'[attached=true,rotation=15]',bystate)
        for selector,want in (('rotation=3',3),('attached=true',17),('attached=false,rotation=3',1)):
            self.assertEqual(len(compiler.block(block,{'variants':{selector:application}})),want)
        for state in ({},{'rotation':'0'},{'attached':'false'}):
            with self.assertRaisesRegex(mod.ImportError,'explicit palette'):
                compiler.compile_model(application,depth=1,selector=state,block_name=block)
        self.assertTrue(compiler.compile_model(application,depth=1,selector={'rotation':'0','attached':'true'},block_name=block)['fixed_rotation'])
        for state in ({'attached':'yes'},{'rotation':'16'}):
            with self.assertRaises(mod.ImportError):self.compile(state=state)
        many={'variants':{f'slot={i}':application for i in range(81)}}
        with self.assertRaisesRegex(mod.ImportError,'4096'):
            canvas.expand_canvas_sign(compiler,block,many,mod.ImportError)

    def test_texture_alpha_cache_and_rollback_reload(self):
        compiler=self.compiler();calls=[]
        reference='farmersdelight:entity/signs/hanging/canvas'
        def decode(png):
            calls.append(png)
            return mod.decode_png(png)
        canvas.texture(compiler,reference,mod.ImportError,decode)
        canvas.texture(compiler,reference,mod.ImportError,decode)
        self.assertEqual(len(calls),1)
        compiler.remove_last_texture()
        self.assertNotIn(reference,compiler._sign_checked_textures)
        canvas.texture(compiler,reference,mod.ImportError,decode)
        self.assertEqual(len(calls),2)
        self.assertIsNot(calls[0],calls[1])
        compiler.textures[reference]=(mod.encode_png(1,1,bytes((2,4,8,127))),[2,4,8],127)
        with self.assertRaisesRegex(mod.ImportError,'binary alpha'):
            canvas.texture(compiler,reference,mod.ImportError,decode)
        self.assertEqual(len(calls),3)

    def test_failed_state_releases_validated_png_cache(self):
        compiler=self.compiler()
        # Simulate a later application failure after successful body validation.
        # Exercise Compiler.block rollback rather than only the removal helper.
        application={'model':canvas.MODEL}
        original=compiler.compile_model
        def fail_after_sign(value,**kwargs):
            original(application,**kwargs)
            raise mod.ImportError('later application failed')
        compiler.compile_model=fail_after_sign
        with self.assertRaisesRegex(mod.ImportError,'later application'):
            compiler.block('farmersdelight:hanging_canvas_sign',{'variants':{'':application}})
        self.assertEqual(compiler.textures,{})
        self.assertEqual(compiler._sign_checked_textures,{})
        self.assertEqual(compiler.element_count,0)

    def test_pixels_overrides_and_old_identity_boundaries(self):
        compiler=self.compiler()
        for block,blue in (('farmersdelight:hanging_canvas_sign',90),('farmersdelight:red_hanging_canvas_sign',210)):
            reference=self.compile(compiler,block)['polygons'][0]['texture']
            w,h,pixels=mod.decode_png(compiler.textures[reference][0])
            self.assertEqual((w,h),(64,64))
            offset=(31*64+23)*4
            self.assertEqual(tuple(pixels[offset:offset+4]),(92,120,blue,255))
        put(self.root,'assets/farmersdelight/textures/entity/signs/hanging/canvas.png',mod.encode_png(2,1,bytes((8,16,32,255))*2))
        compiler=self.compiler();reference=self.compile(compiler)['polygons'][0]['texture']
        self.assertEqual(mod.decode_png(compiler.textures[reference][0])[2],bytes((8,16,32,255))*4)
        with self.assertRaisesRegex(mod.ImportError,'standard elements'):
            self.compile(block='dyenamicsandfriends:farmersdelight_amber_hanging_canvas_sign')
        visible={'textures':{'all':'farmersdelight:entity/signs/hanging/canvas'},'elements':[{'from':[0,0,0],'to':[1,1,1],
                 'faces':{'up':{'texture':'#all'}}}]}
        put(self.root,'assets/farmersdelight/models/block/canvas_sign.json',visible)
        self.assertNotIn('fixed_rotation',self.compile())

    def test_shared_budget_ignored_wrapper_and_catalog(self):
        compiler=self.compiler()
        self.assertEqual(self.compile(compiler),self.compile(compiler,x=90,y=270,uvlock=True))
        compiler.element_count=mod.MAX_COMPILED_ELEMENTS-13
        with self.assertRaisesRegex(mod.ImportError,'budget'):
            self.compile(compiler)
        report=mod.compile_pack([self.root],self.root/'pack')
        self.assertEqual((report['blocks'],report['selectors'],report['textures']),(4,112,2))
        self.assertEqual(report['unsupported'],[]);self.assertEqual(report['unsupported_states'],[])


if __name__=='__main__':unittest.main()
