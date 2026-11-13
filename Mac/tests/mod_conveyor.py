"""Literal IE conveyor geometry, source ownership, and bounded palette defaults."""
from collections import Counter
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_conveyor_fixtures import conveyor_assets


class ConveyorModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        conveyor_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, compiler=None, kind='basic', selector=None, model=None, **kwargs):
        return (compiler or self.compiler()).compile_model(
            {'model': model or 'immersiveengineering:block/conveyor_'+kind, **kwargs}, selector=selector)

    def test_literal_source_positions_winding_uv_and_texture_pixels(self):
        compiler = self.compiler()
        result = self.compile(compiler)
        faces = result['polygons']
        self.assertEqual(len(faces),35)
        self.assertEqual(result['rotation'],(0,0,0))
        self.assertTrue(result['fixed_rotation'])
        # Independent source literals fix both the inverted bottom and top UVs.
        self.assertEqual(faces[0]['vertices'],[(15,0,0,15,0),(15,0,16,15,16),(1,0,16,1,16),(1,0,0,1,0)])
        self.assertEqual(faces[2]['vertices'],[(0,2,0,0,16),(0,2,16,0,0),(16,2,16,16,0),(16,2,0,16,16)])
        self.assertEqual(faces[29]['vertices'],[(0,3,1,0,15),(0,3,15,0,1),(1,3,15,1,1),(1,3,1,1,15)])
        self.assertEqual(Counter(p['texture'].rsplit('/',1)[1] for p in faces),{'off':4,'casing_side':14,'casing_walls':17})
        for polygon in faces:
            a,b,c = (v[:3] for v in polygon['vertices'][:3])
            ab,ac = [b[i]-a[i] for i in range(3)],[c[i]-a[i] for i in range(3)]
            self.assertTrue(any(ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3] for i in range(3)))
        for name,(png,*_) in compiler.textures.items():
            expected = (self.root/mod.asset_path(name,'textures','.png')).read_bytes()
            self.assertEqual(mod.decode_png(png),mod.decode_png(expected))

    def test_cardinal_and_vertical_geometry_preserves_uvs(self):
        north = self.compile()['polygons']
        yaw = {'north':lambda x,y,z:(x,y,z),'east':lambda x,y,z:(16-z,y,x),
               'south':lambda x,y,z:(16-x,y,16-z),'west':lambda x,y,z:(z,y,16-x)}
        for kind in ('basic','dropper','vertical'):
            for facing,turn in yaw.items():
                faces = self.compile(kind=kind,selector={'facing':facing})['polygons']
                for original,actual in zip(north,faces):
                    for old,new in zip(original['vertices'],actual['vertices']):
                        x,y,z = old[:3]
                        point = (x,16-z,y) if kind=='vertical' else (x,y,z)
                        self.assertEqual(new[:3],turn(*point))
                        self.assertEqual(new[3:],old[3:])
                belt = {'basic':'off','dropper':'dropper_off','vertical':'vertical_off'}[kind]
                self.assertTrue(faces[0]['texture'].endswith('/'+belt))
        points = [v[:3] for p in self.compile(kind='vertical')['polygons'] for v in p['vertices']]
        self.assertEqual(tuple(min(p[i] for p in points) for i in range(3)),(0,0,0))
        self.assertEqual(tuple(max(p[i] for p in points) for i in range(3)),(16,16,3))

    def test_ignored_wrapper_context_and_loader_owned_type(self):
        compiler = self.compiler()
        plain = self.compile(compiler)
        self.assertEqual(plain,self.compile(compiler,x=90,y=270,uvlock=True,**{'ae2:z':90}))
        child = {'parent':'immersiveengineering:block/conveyor_basic','conveyorType':'immersiveengineering:vertical'}
        self.assertEqual(plain,self.compile(compiler,model=child))
        with self.assertRaisesRegex(mod.ImportError,'unsupported IE conveyor type'):
            self.compile(compiler,model={'parent':child['parent'],'loader':mod._conveyor_module.LOADER})
        self.assertEqual(self.compile(compiler,kind='vertical'),self.compile(compiler,model={**child,'loader':mod._conveyor_module.LOADER}))
        self.assertTrue(any('ignored by the shipped' in w for w in compiler.warnings))
        self.assertTrue(any('order-dependent' in w for w in compiler.warnings))

    def test_nested_facing_guard_and_nonidentity_transform_boundary(self):
        outer = {'loader':'neoforge:composite','children':{'belt':{'parent':'immersiveengineering:block/conveyor_basic'}}}
        with self.assertRaisesRegex(mod.ImportError,'requires explicit facing'):
            self.compile(model=outer)
        self.assertEqual(len(self.compile(model=outer,selector={'facing':'east'})['polygons']),35)
        for transform in ({'translation':[1,0,0]},{'rotation':[0,90,0]},{'scale':[2,1,1]}):
            with self.assertRaisesRegex(mod.ImportError,'fixed|rotation'):
                self.compile(model={**outer,'transform':transform},selector={'facing':'east'})

    def test_expansion_preserves_properties_and_rejects_ambiguous_or_malformed_states(self):
        compiler = self.compiler()
        application = {'model':'immersiveengineering:block/conveyor_basic'}
        expand = lambda d: mod._conveyor_module.expand_conveyor(compiler,'fixture:test',d,mod.ImportError)
        variants = expand({'variants':{'waterlogged=true':application}})['variants']
        self.assertEqual(len(variants),5)
        self.assertIn('facing=west,waterlogged=true',variants)
        for selector in ('facing=up','a=true,a=false','a','A=1'):
            with self.subTest(selector=selector),self.assertRaises(mod.ImportError):
                expand({'variants':{selector:application}})
        with self.assertRaisesRegex(mod.ImportError,'overlapping'):
            expand({'variants':{'':application,'facing=east':{**application,'y':90}}})
        with self.assertRaisesRegex(mod.ImportError,'multipart'):
            expand({'multipart':[{'apply':application}]})

    def test_texture_override_missing_source_type_and_budget(self):
        override = self.root/'override'
        texture = 'assets/immersiveengineering/textures/block/conveyor/off.png'
        put(override,texture,mod.encode_png(1,1,bytes((11,22,33,255))))
        compiler = self.compiler([self.root,override])
        self.compile(compiler)
        self.assertEqual(mod.decode_png(compiler.textures['immersiveengineering:block/conveyor/off'][0])[2],bytes((11,22,33,255)))
        compiler.element_count = mod._conveyor_module.MAX_COMPILED_ELEMENTS-34
        with self.assertRaisesRegex(mod.ImportError,'budget'):
            self.compile(compiler)
        for value in (None,{},'immersiveengineering:extract'):
            with self.assertRaises(mod.ImportError):
                self.compile(model={'loader':mod._conveyor_module.LOADER,'conveyorType':value})
        (self.root/texture).unlink()
        with self.assertRaisesRegex(mod.ImportError,'missing texture'):
            self.compile()

    def test_shared_catalog_serialization(self):
        report = mod.compile_pack([self.root],self.root/'pack')
        self.assertEqual((report['blocks'],report['selectors'],report['textures']),(3,15,5))
        self.assertEqual(report['unsupported'],[])
        self.assertEqual(report['unsupported_states'],[])
        text = (self.root/'pack/manifest.mwpack').read_text()
        self.assertEqual(text.count('usemodel\t'),15)


if __name__ == '__main__':
    unittest.main()
