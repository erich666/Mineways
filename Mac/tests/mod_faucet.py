"""Faucet child ownership, dry geometry, and shared validation boundaries."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_faucet_fixtures import faucet_assets
from mod_conveyor_fixtures import conveyor_assets


class FaucetModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.models = faucet_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, compiler=None, model='fixture:faucet_off', selector=None, **rotation):
        return (compiler or self.compiler()).compile_model({'model':model,**rotation},selector=selector)

    def wrapper(self, **changes):
        return {'loader':'supplementaries:faucet','model':'fixture:faucet_body_off',
                'liquid':'fixture:faucet_body_liquid',**changes}

    def test_dry_body_exact_geometry_uv_cull_and_application_turns(self):
        compiler = self.compiler()
        for label in ('off','on'):
            for turns in ({},{'y':90},{'y':180},{'y':270},{'x':90,'y':270,'uvlock':True},{'ae2:z':90}):
                result = self.compile(compiler,model='fixture:faucet_'+label,**turns)
                expected = self.compile(compiler,model='fixture:faucet_body_'+label,**turns)
                self.assertEqual(result,expected)
                self.assertNotIn('fixed_rotation',result)
                faces = result['elements'][0]['faces']
                self.assertEqual(next(f for f in faces if f[0]=='north')[-1],'north')
                self.assertEqual(next(f for f in faces if f[0]=='up')[:6],('up',1,2,13,15,90))
        self.assertTrue(any('dry faucet body' in w for w in compiler.warnings))
        self.assertTrue(any('outer faucet' in w for w in compiler.warnings))
        self.assertNotIn('fixture:missing_outer',compiler.textures)

    def test_inline_children_and_inherited_loader_fields(self):
        compiler = self.compiler()
        expected = self.compile(compiler)
        self.assertEqual(self.compile(compiler,model=self.wrapper(model=self.models['off'],liquid=self.models['liquid'])),expected)
        child = {'parent':'fixture:faucet_off','model':'fixture:faucet_body_on','liquid':'fixture:absent'}
        self.assertEqual(self.compile(compiler,model=child),expected)
        with self.assertRaisesRegex(mod.ImportError,'liquid'):
            self.compile(compiler,model={'parent':'fixture:faucet_off','loader':'supplementaries:faucet',
                                         'model':'fixture:faucet_body_on'})
        own = {**child,'loader':'supplementaries:faucet','liquid':'fixture:faucet_body_liquid'}
        self.assertEqual(self.compile(compiler,model=own),self.compile(compiler,model='fixture:faucet_on'))

    def test_both_children_validate_dependencies_and_shapes(self):
        for field in ('model','liquid'):
            for value in (None,[],1,False,'fixture:absent',{'parent':'fixture:absent'}, {'elements':[]}):
                with self.subTest(field=field,value=value),self.assertRaises(mod.ImportError):
                    self.compile(model=self.wrapper(**{field:value}))
        liquid = copy.deepcopy(self.models['liquid'])
        liquid['textures']['body'] = 'fixture:missing_liquid'
        with self.assertRaisesRegex(mod.ImportError,'missing texture'):
            self.compile(model=self.wrapper(liquid=liquid))

    def test_child_root_transform_and_nested_split_boundaries(self):
        for field in ('model','liquid'):
            source = self.models['off' if field=='model' else 'liquid']
            with self.assertRaisesRegex(mod.ImportError,'child root transform'):
                self.compile(model=self.wrapper(**{field:{**source,'transform':{'translation':[1,0,0]}}}))
        # A composite child has its own supported transform implementation.
        body = {'loader':'neoforge:composite','transform':{'translation':[1,0,0]},
                'children':{'body':self.models['off']}}
        with self.assertRaisesRegex(mod.ImportError,'transformed composites with standard box'):
            self.compile(model=self.wrapper(model=body))
        put(self.root,'assets/fixture/models/faucet.obj',b'mtllib faucet.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl body\nf 1/1 2/2 3/3 4/4\n')
        put(self.root,'assets/fixture/models/faucet.mtl',b'newmtl body\nmap_Kd fixture:faucet_off\n')
        obj = {'loader':'neoforge:obj','model':'fixture:models/faucet.obj','transform':{'translation':[1,0,0]}}
        self.assertEqual(len(self.compile(model=self.wrapper(model=obj))['polygons']),1)
        nested = {'loader':'neoforge:composite','children':{'tap':{'parent':'fixture:faucet_off'}}}
        self.assertEqual(self.compile(model=nested)['elements'],self.compile()['elements'])
        # XyCraft pipe is a verified split-model loader, but cannot be used as
        # either faucet child without preserving its separate part applications.
        pipe = {'loader':'xycraft_machines:pipe',**{key:self.models['off'] for key in ('core','cap','extract','insert','short_straight')}}
        for field in ('model','liquid'):
            with self.assertRaisesRegex(mod.ImportError,'nested split faucet'):
                self.compile(model=self.wrapper(**{field:pipe}))

    def test_recursive_children_share_depth_limit(self):
        put(self.root,'assets/fixture/models/faucet_cycle.json',self.wrapper(model='fixture:faucet_cycle'))
        with self.assertRaisesRegex(mod.ImportError,'depth'):
            self.compile(model='fixture:faucet_cycle')

    def test_supplied_resource_override_and_body_scope(self):
        override = self.root/'override'
        body = copy.deepcopy(self.models['off'])
        body['elements'][0]['from'] = [1,2,3]
        put(override,'assets/fixture/models/faucet_body_off.json',body)
        pixels = bytes((21,42,84,255))*4
        put(override,'assets/fixture/textures/faucet_off.png',mod.encode_png(2,2,pixels))
        compiler = self.compiler([self.root,override])
        self.assertEqual(self.compile(compiler)['elements'][0]['coordinates'][:3],(1,2,3))
        self.assertEqual(mod.decode_png(compiler.textures['fixture:faucet_off'][0])[2],pixels)

    def test_liquid_consumes_budget_and_failed_state_rolls_back(self):
        compiler = self.compiler()
        compiler.element_count = mod.MAX_COMPILED_ELEMENTS-1
        before = compiler.element_count
        with self.assertRaisesRegex(mod.ImportError,'budget|limit'):
            compiler.block('fixture:test',{'variants':{'':{'model':'fixture:faucet_off'}}})
        self.assertEqual(compiler.element_count,before)
        self.assertEqual(len(compiler.textures),0)
        self.assertEqual(compiler.warnings,set())

    def test_custom_body_preserves_fixed_rotation_and_palette_requirements(self):
        conveyor_assets(self.root,mod.encode_png)
        body = {'loader':'immersiveengineering:models/conveyor','conveyorType':'immersiveengineering:basic'}
        with self.assertRaisesRegex(mod.ImportError,'requires explicit facing'):
            self.compile(model=self.wrapper(model=body))
        result = self.compile(model=self.wrapper(model=body),selector={'facing':'east'})
        self.assertTrue(result['fixed_rotation'])
        self.assertEqual(len(result['polygons']),35)
        nested = {'loader':'neoforge:composite','transform':{'translation':[1,0,0]},
                  'children':{'faucet':self.wrapper(model=body)}}
        with self.assertRaisesRegex(mod.ImportError,'fixed|rotation'):
            self.compile(model=nested,selector={'facing':'east'})

    def test_shared_catalog_contains_only_body_textures(self):
        report = mod.compile_pack([self.root],self.root/'pack')
        self.assertEqual((report['blocks'],report['selectors'],report['textures']),(2,10,2))
        self.assertEqual(report['unsupported'],[])
        self.assertEqual(report['unsupported_states'],[])
        manifest = (self.root/'pack/manifest.mwpack').read_text()
        self.assertNotIn('fixture:faucet_liquid',manifest)
        self.assertEqual(manifest.count('usemodel\t'),10)


if __name__=='__main__':
    unittest.main()
