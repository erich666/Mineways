"""Canvas sign source cubes, all palette headings, and narrow renderer fallback."""
import math
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_canvas_sign_fixtures import canvas_sign_assets


class CanvasSignModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        canvas_sign_assets(self.root,mod.encode_png)

    def compiler(self,sources=None):
        assets=mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self,compiler=None,block='farmersdelight:canvas_sign',state=None,model='farmersdelight:block/canvas_sign',**turn):
        return (compiler or self.compiler()).compile_model({'model':model,**turn},selector=state,block_name=block)

    def test_literal_source_board_post_uv_and_winding(self):
        result=self.compile()
        faces=result['polygons']
        self.assertTrue(result['fixed_rotation'])
        self.assertEqual(result['rotation'],(0,0,0))
        self.assertEqual(len(faces),12)
        s=0.6666666865348816
        # Source DOWN: vertex5,4,0,1. Reflection of Y and Z preserves winding.
        self.assertEqual(faces[0]['vertices'],[(8+12*s,8+14*s,8-s,6.5,0),
            (8-12*s,8+14*s,8-s,.5,0),(8-12*s,8+14*s,8+s,.5,1),(8+12*s,8+14*s,8+s,6.5,1)])
        self.assertEqual([v[3:] for v in faces[1]['vertices']],[(12.5,1),(6.5,1),(6.5,0),(12.5,0)])
        self.assertEqual([v[3:] for v in faces[6]['vertices']],[(1,7),(.5,7),(.5,8),(1,8)])
        self.assertEqual([v[3:] for v in faces[11]['vertices']],[(2,8),(1.5,8),(1.5,15),(2,15)])
        for group in (faces[:6],faces[6:]):
            points=[v[:3] for f in group for v in f['vertices']]
            center=[(min(p[i] for p in points)+max(p[i] for p in points))/2 for i in range(3)]
            for face in group:
                a,b,c=(v[:3] for v in face['vertices'][:3])
                ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
                normal=[ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)]
                self.assertGreater(sum(normal[i]*(a[i]-center[i]) for i in range(3)),0)

    def test_all_sixteen_angles_and_default_zero(self):
        baseline=self.compile()['polygons']
        self.assertEqual(baseline,self.compile(state={'rotation':'0'})['polygons'])
        for rotation in range(16):
            faces=self.compile(state={'rotation':str(rotation)})['polygons']
            theta=math.radians(22.5*rotation)
            for source,actual in zip(baseline,faces):
                for a,b in zip(source['vertices'],actual['vertices']):
                    x,z=a[0]-8,a[2]-8
                    self.assertAlmostEqual(b[0],8+math.cos(theta)*x-math.sin(theta)*z)
                    self.assertAlmostEqual(b[2],8+math.sin(theta)*x+math.cos(theta)*z)
                    self.assertEqual(a[1],b[1]);self.assertEqual(a[3:],b[3:])
        # Independent quarter-turn points fix the yaw sign and pivot.
        self.assertAlmostEqual(self.compile(state={'rotation':'4'})['polygons'][0]['vertices'][0][0],8+0.6666666865348816)
        self.assertAlmostEqual(self.compile(state={'rotation':'4'})['polygons'][0]['vertices'][0][2],16.00000023841858)

    def test_wall_defaults_cardinal_heading_and_no_post(self):
        block='farmersdelight:canvas_wall_sign'
        self.assertEqual(self.compile(block=block),self.compile(block=block,state={'facing':'north'}))
        s=0.6666666865348816
        bounds={'north':((8-12*s,3+2*s,15-s),(8+12*s,3+14*s,15+s)),
                'south':((8-12*s,3+2*s,1-s),(8+12*s,3+14*s,1+s)),
                'east':((1-s,3+2*s,8-12*s),(1+s,3+14*s,8+12*s)),
                'west':((15-s,3+2*s,8-12*s),(15+s,3+14*s,8+12*s))}
        for facing,(low,high) in bounds.items():
            faces=self.compile(block=block,state={'facing':facing})['polygons']
            self.assertEqual(len(faces),6)
            points=[v[:3] for p in faces for v in p['vertices']]
            for axis in range(3):
                self.assertAlmostEqual(min(p[axis] for p in points),low[axis])
                self.assertAlmostEqual(max(p[axis] for p in points),high[axis])

    def test_exact_identity_route_and_visible_resource_override(self):
        for name in ('dyenamicsandfriends:farmersdelight_amber_canvas_sign','farmersdelight:hanging_canvas_sign','farmersdelight:blue_hanging_canvas_sign','fixture:canvas_sign'):
            with self.assertRaisesRegex(mod.ImportError,'standard elements'):
                self.compile(block=name)
        put(self.root,'assets/fixture/models/unrelated.json',{'textures':{}})
        with self.assertRaisesRegex(mod.ImportError,'standard elements'):
            self.compile(model='fixture:unrelated')
        source={'textures':{'all':'farmersdelight:entity/signs/canvas'},'elements':[{'from':[1,2,3],'to':[4,5,6],
                'faces':{'up':{'texture':'#all','uv':[0,0,16,16]}}}]}
        override=self.root/'override'
        put(override,'assets/farmersdelight/models/block/canvas_sign.json',source)
        compiler=self.compiler([self.root,override])
        result=self.compile(compiler)
        self.assertNotIn('fixed_rotation',result)
        self.assertEqual(result['elements'][0]['coordinates'][:6],(1,2,3,4,5,6))
        definition={'variants':{'':{'model':'farmersdelight:block/canvas_sign'}}}
        self.assertEqual(len(compiler.block('farmersdelight:canvas_sign',definition)),1)

    def test_texture_colors_rectangular_normalization_and_override(self):
        compiler=self.compiler()
        for block,blue in (('farmersdelight:canvas_sign',60),('farmersdelight:red_canvas_sign',180)):
            texture=self.compile(compiler,block=block)['polygons'][0]['texture']
            width,height,pixels=mod.decode_png(compiler.textures[texture][0])
            self.assertEqual((width,height),(64,64))
            for x,y in ((0,0),(20,30),(63,63)):
                self.assertEqual(tuple(pixels[(y*64+x)*4:(y*64+x)*4+4]),(4*x,8*(y//2),blue,255))
        override=self.root/'override'
        put(override,'assets/farmersdelight/textures/entity/signs/canvas.png',mod.encode_png(2,1,bytes((11,22,33,255))*2))
        compiler=self.compiler([self.root,override]);texture=self.compile(compiler)['polygons'][0]['texture']
        self.assertEqual(mod.decode_png(compiler.textures[texture][0])[2],bytes((11,22,33,255))*4)

    def test_invalid_palette_missing_texture_and_alpha(self):
        for state in ({'rotation':'16'},{'rotation':'-1'},{'rotation':'1.0'},{'rotation':'01'}):
            with self.assertRaisesRegex(mod.ImportError,'rotation'):
                self.compile(state=state)
        with self.assertRaisesRegex(mod.ImportError,'facing'):
            self.compile(block='farmersdelight:canvas_wall_sign',state={'facing':'up'})
        path=self.root/'assets/farmersdelight/textures/entity/signs/canvas.png'
        path.write_bytes(mod.encode_png(1,1,bytes((11,22,33,128))))
        with self.assertRaisesRegex(mod.ImportError,'binary alpha'):
            self.compile()
        path.unlink()
        with self.assertRaisesRegex(mod.ImportError,'missing texture'):
            self.compile()

    def test_ignored_application_and_nested_transform_boundary(self):
        expected=self.compile()
        self.assertEqual(expected,self.compile(x=90,y=270,uvlock=True,**{'ae2:z':90}))
        outer={'loader':'neoforge:composite','children':{'sign':{'parent':'farmersdelight:block/canvas_sign'}}}
        # Exact resource-only fallback does not guess identity for inline children.
        with self.assertRaises(mod.ImportError):
            self.compile(model=outer,state={'rotation':'0'})
        compiler=self.compiler()
        with self.assertRaisesRegex(mod.ImportError,'nested canvas sign'):
            compiler.compile_model({'model':'farmersdelight:block/canvas_sign'},depth=1,block_name='farmersdelight:canvas_sign')
        self.assertTrue(compiler.compile_model({'model':'farmersdelight:block/canvas_sign'},depth=1,
                        block_name='farmersdelight:canvas_sign',selector={'rotation':'0'})['fixed_rotation'])

    def test_expansion_properties_budget_and_serialization(self):
        compiler=self.compiler()
        definition={'variants':{'waterlogged=true':{'model':'farmersdelight:block/canvas_sign'}}}
        rows=compiler.block('farmersdelight:canvas_sign',definition)
        self.assertEqual(len(rows),17)
        self.assertTrue(all('waterlogged=true' in r[0] for r in rows))
        compiler=self.compiler();compiler.element_count=mod.MAX_COMPILED_ELEMENTS-11
        with self.assertRaisesRegex(mod.ImportError,'budget'):
            compiler.block('farmersdelight:canvas_sign',definition)
        self.assertEqual(compiler.element_count,mod.MAX_COMPILED_ELEMENTS-11)
        self.assertEqual(len(compiler.textures),0)
        report=mod.compile_pack([self.root],self.root/'pack')
        self.assertEqual((report['blocks'],report['selectors'],report['textures']),(4,44,2))
        self.assertEqual(report['unsupported'],[])
        self.assertEqual(report['unsupported_states'],[])


if __name__=='__main__':
    unittest.main()
