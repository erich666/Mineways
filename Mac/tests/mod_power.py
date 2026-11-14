"""Static disconnected pipes and inactive energy cubes retain source geometry."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_power_fixtures import power_assets

spec=importlib.util.spec_from_file_location('power_helper',Path(__file__).resolve().parents[2]/'tools/mod_power_models.py')
power=importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class PowerDefaults(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='mineways-power-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        self.cube=power_assets(self.root,mod.encode_png)

    def compiler(self):
        assets=mod.Assets([self.root]);self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, model, rotation=(0,0,0), depth=0):
        compiler=self.compiler()
        result=power.compile_power_model(compiler,'fixture:test',model,rotation,{},depth,mod.ImportError)
        return compiler,result

    def pipe(self, transparent=False, **extra):
        return {'loader':power.PIPE_LOADER,'pipeType':'fixture_'+('transparent' if transparent else 'opaque'),
                'transparent':transparent,**extra}

    def test_pipe_exact_cube_winding_and_position_uvs(self):
        for transparent in (False,True):
            compiler,result=self.compile(self.pipe(transparent))
            self.assertEqual(result['rotation'],(0,0,0));self.assertTrue(result['fixed_rotation'])
            self.assertEqual(len(result['polygons']),12 if transparent else 6)
            self.assertEqual(len(compiler.textures),1)
            for face in result['polygons']:
                points=face['vertices'];a,b,c=points[:3]
                ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
                cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                self.assertEqual(sum(abs(v) for v in cross),36)
                direction=next(side for side,normal in {'down':(0,-36,0),'up':(0,36,0),'north':(0,0,-36),
                    'south':(0,0,36),'west':(-36,0,0),'east':(36,0,0)}.items() if normal==cross)
                for x,y,z,u,v in points:
                    self.assertTrue(all(p in (5,11) for p in (x,y,z)))
                    expected={'down':(x,16-z),'up':(x,z),'north':(16-x,16-y),
                              'south':(x,16-y),'west':(z,16-y),'east':(16-z,16-y)}[direction]
                    self.assertEqual((u,v),expected)
            encoded=next(iter(compiler.textures.values()))[0]
            self.assertEqual(tuple(mod.decode_png(encoded)[2][(7*16+5)*4:(7*16+5)*4+4]),(80,112,70,128 if transparent else 255))
            if transparent:
                for first,second in zip(result['polygons'][::2],result['polygons'][1::2]):
                    self.assertEqual([v[:3] for v in first['vertices']],list(reversed([v[:3] for v in second['vertices']])))
                    self.assertNotEqual([v[3:] for v in first['vertices']],list(reversed([v[3:] for v in second['vertices']])))

    def test_pipe_ignores_context_turns_and_has_no_connections(self):
        _,reference=self.compile(self.pipe())
        compiler,actual=self.compile(self.pipe(transform={'translation':[1,2,3]},visibility={'all':False},elements=[]),(90,270,1,90))
        self.assertEqual(actual,reference)
        self.assertTrue(any('disconnected block default' in s for s in compiler.warnings))
        self.assertTrue(any('ignored by the shipped' in s for s in compiler.warnings))

    def test_pipe_rejects_invalid_or_unsafe_resources_and_options(self):
        for kind in (None,[],2,'','../secret','foo/../bar','minecraft:stone','a'*901,'missing'):
            with self.subTest(kind=kind),self.assertRaises(mod.ImportError):self.compile(self.pipe(pipeType=kind))
        for transparent in (None,0,1,'true',[]):
            with self.subTest(value=transparent),self.assertRaises(mod.ImportError):self.compile(self.pipe(transparent=transparent))
        with self.assertRaisesRegex(mod.ImportError,'depth'):self.compile(self.pipe(),depth=63)

    def test_cube_only_frame_and_unmodified_leds_preserves_turns_and_pixels(self):
        for rotation in ((0,0,0),(0,90,1),(270,0,0),(0,0,1,90)):
            compiler,result=self.compile(self.cube,rotation)
            selected=self.cube['frame']+[e for side in power.SIDES for e in self.cube[side+'LEDs']]
            application={'model':{'textures':self.cube['textures'],'elements':selected},'x':rotation[0],'y':rotation[1],'uvlock':bool(rotation[2])}
            if len(rotation)>3:application['ae2:z']=rotation[3]
            expected=self.compiler().compile_model(application)
            self.assertEqual(result,expected)
            self.assertEqual(len(result['elements']),7)
            self.assertEqual(sum(len(e['faces']) for e in result['elements']),7)
            self.assertEqual(set(compiler.textures),{'fixture:power_frame','fixture:power_led'})
            for name,(encoded,_,_) in compiler.textures.items():
                self.assertEqual(mod.decode_png(encoded),mod.decode_png((self.root/mod.asset_path(name,'textures','.png')).read_bytes()))
            self.assertTrue(any('stored-energy' in s for s in compiler.warnings))

    def test_cube_element_rotation_and_face_metadata_survive(self):
        model=copy.deepcopy(self.cube)
        model['frame'][0]['rotation']={'axis':'y','angle':22.5,'origin':[7,8,9],'rescale':True}
        model['frame'][0]['faces']['north']['neoforge_data']={'color':'80ff8080'}
        compiler,result=self.compile(model)
        self.assertEqual(result['elements'][0]['coordinates'][6:],('y',22.5,7,8,9,1))
        face=result['elements'][0]['faces'][0]
        self.assertNotEqual(face[6],'fixture:power_frame')
        self.assertEqual(mod.decode_png(compiler.textures[face[6]][0])[2][3],128)

    def test_cube_rejects_missing_empty_overlarge_arrays_and_root_transform(self):
        for field in power.CUBE_FIELDS:
            for value in (None,[],{},[None]):
                model={**self.cube,field:value}
                with self.subTest(field=field,value=value),self.assertRaises(mod.ImportError):self.compile(model)
        with self.assertRaisesRegex(mod.ImportError,'count'):
            self.compile({**self.cube,'frame':self.cube['frame']*2048})
        with self.assertRaisesRegex(mod.ImportError,'root transforms'):
            self.compile({**self.cube,'transform':{'translation':[1,0,0]}})

    def test_integrated_ownership_and_selector_identity(self):
        compiler=self.compiler()
        for name,count in [('pipe_opaque',2),('pipe_transparent',2),('energy_cube',3)]:
            rows=compiler.block('fixture:'+name,compiler.assets.json('assets/fixture/blockstates/'+name+'.json'))
            self.assertEqual(len(rows),count)
        self.assertFalse(compiler.unsupported_states)
        for name,extra in [('pipe_opaque',{'pipeType':'missing','transparent':None}),('energy_cube',{'frame':[]})]:
            put(self.root,'assets/fixture/models/child.json',{'parent':'fixture:'+name,**extra})
            c=self.compiler()
            self.assertEqual(c.compile_model({'model':'fixture:child'}),c.compile_model({'model':'fixture:'+name}))
            put(self.root,'assets/fixture/models/new.json',{'parent':'fixture:'+name,'loader':power.PIPE_LOADER if name.startswith('pipe') else power.CUBE_LOADER})
            with self.assertRaises(mod.ImportError):self.compiler().compile_model({'model':'fixture:new'})


if __name__=='__main__':unittest.main()
