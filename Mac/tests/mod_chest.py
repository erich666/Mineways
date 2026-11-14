"""Chest halves keep exact entity UVs, inherited materials and palette facing."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from mod_assets import mod
from mod_chest_fixtures import chest_assets

spec = importlib.util.spec_from_file_location('chest_adapter',Path(__file__).parents[2]/'tools/mod_chest_models.py')
chest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chest)


class StorageChests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.model = chest_assets(self.root,mod.encode_png)

    def compiler(self):
        assets = mod.Assets([self.root]);self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def put(self,path,value):
        destination=self.root/path;destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(value if isinstance(value,bytes) else json.dumps(value).encode())

    def compile(self,facing=None,shape=None,tier='wood',compiler=None,rotation=(0,0,0),depth=0):
        compiler=compiler or self.compiler()
        state={}
        if facing is not None:state['facing']=facing
        if shape is not None:state['type']=shape
        block='sophisticatedstorage:'+(tier+'_' if tier!='wood' else '')+'chest'
        return compiler,chest.compile_chest(compiler,'sophisticatedstorage:block/chest',self.model,rotation,state,
                                            depth,mod.ImportError,block,mod.decode_png)

    def test_shared_polygon_budget_exact_boundary(self):
        compiler = self.compiler()
        compiler.element_count = chest.MAX_COMPILED_ELEMENTS - 18
        self.compile(compiler=compiler)
        self.assertEqual(compiler.element_count, chest.MAX_COMPILED_ELEMENTS)
        compiler = self.compiler()
        compiler.element_count = chest.MAX_COMPILED_ELEMENTS - 17
        before = compiler.element_count
        with self.assertRaisesRegex(mod.ImportError, 'polygon budget'):
            self.compile(compiler=compiler)
        self.assertEqual(compiler.element_count, before)
        self.assertEqual(len(compiler.textures), 0)
        self.assertEqual(compiler.face_texture_pixel_count, 0)

    def test_literal_cube_bounds_and_uv_rectangles(self):
        _,model=self.compile('south','single')
        self.assertEqual(len(model['polygons']),18)
        rectangles=((14,19,28,33),(28,33,42,19),(0,33,14,43),(14,33,28,43),(28,33,42,43),(42,33,56,43),
                    (14,0,28,14),(28,14,42,0),(0,14,14,19),(14,14,28,19),(28,14,42,19),(42,14,56,19),
                    (1,0,3,1),(3,1,5,0),(0,1,1,5),(1,1,3,5),(3,1,4,5),(4,1,6,5))
        for face,(u1,v1,u2,v2) in zip(model['polygons'],rectangles):
            self.assertEqual([v[3:] for v in face['vertices']],[(u2/4,v1/4),(u1/4,v1/4),(u1/4,v2/4),(u2/4,v2/4)])
        for offset,bounds in ((0,((1,0,1),(15,10,15))),(6,((1,9,1),(15,14,15))),(12,((7,7,15),(9,11,16)))):
            points=[v[:3] for face in model['polygons'][offset:offset+6] for v in face['vertices']]
            self.assertEqual(tuple(min(v[d] for v in points) for d in range(3)),bounds[0])
            self.assertEqual(tuple(max(v[d] for v in points) for d in range(3)),bounds[1])
        for shape in ('left','right'):
            _,half=self.compile('south',shape)
            self.assertEqual([v[3:] for v in half['polygons'][0]['vertices']],[(29/4,19/4),(14/4,19/4),(14/4,33/4),(29/4,33/4)])
            self.assertEqual([v[3:] for v in half['polygons'][17]['vertices']],[(1,1/4),(3/4,1/4),(3/4,5/4),(1,5/4)])

    def test_double_halves_join_without_duplicate_chests(self):
        # Source south: RIGHT at x0 has x1..16, LEFT at x16 has x16..31.
        for facing,delta in [('south',(16,0,0)),('north',(-16,0,0)),('east',(0,0,-16)),('west',(0,0,16))]:
            _,right=self.compile(facing,'right');_,left=self.compile(facing,'left')
            rp=[v[:3] for f in right['polygons'][:12] for v in f['vertices']]
            lp=[tuple(v[d]+delta[d] for d in range(3)) for f in left['polygons'][:12] for v in f['vertices']]
            self.assertEqual(len(right['polygons'])+len(left['polygons']),36)
            axis=0 if delta[0] else 2
            self.assertEqual(max(v[axis] for v in rp+lp)-min(v[axis] for v in rp+lp),30)
            latch=[v[:3] for f in right['polygons'][12:] for v in f['vertices']]
            latch += [tuple(v[d]+delta[d] for d in range(3)) for f in left['polygons'][12:] for v in f['vertices']]
            self.assertEqual(max(v[axis] for v in latch)-min(v[axis] for v in latch),2)

    def test_palette_defaults_orientation_winding_and_transforms(self):
        _,default=self.compile()
        self.assertEqual(default,self.compile('north','single')[1])
        self.assertEqual(default,self.compile(rotation=(90,270,1,90))[1])
        self.assertTrue(default['fixed_rotation']);self.assertEqual(default['rotation'],(0,0,0))
        # Latch front face centers must point along the selected world direction.
        expected={'north':(8,9,0),'south':(8,9,16),'west':(0,9,8),'east':(16,9,8)}
        for facing,center in expected.items():
            _,result=self.compile(facing,'single')
            face=result['polygons'][17]['vertices']
            self.assertEqual(tuple(sum(v[d] for v in face)/4 for d in range(3)),center)
            for offset in (0,6,12):
                points=[v for f in result['polygons'][offset:offset+6] for v in f['vertices']]
                midpoint=[sum(v[d] for v in points)/24 for d in range(3)]
                for face in result['polygons'][offset:offset+6]:
                    a,b,c=face['vertices'][:3];ab=[b[d]-a[d] for d in range(3)];ac=[c[d]-a[d] for d in range(3)]
                    normal=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                    self.assertGreater(sum(normal[d]*(a[d]-midpoint[d]) for d in range(3)),0)

    def test_tier_composition_latch_scope_and_cache_budget(self):
        compiler=self.compiler()
        for index,tier in enumerate(('wood','copper','iron','gold','diamond','netherite')):
            for shape_index,shape in enumerate(('single','left','right')):
                _,model=self.compile('north',shape,tier,compiler)
                body=model['polygons'][0]['texture'];latch=model['polygons'][12]['texture']
                self.assertNotEqual(body,latch)
                rgba=mod.decode_png(compiler.textures[body][0])[2]
                marked=(20*64+21)*4;clear=(20*64+22)*4
                self.assertEqual(tuple(rgba[marked:marked+4]),(30+35*index,210,90+40*shape_index,255))
                self.assertEqual(tuple(rgba[clear:clear+4]),(88,80,40+40*shape_index,255))
                tier_rgba=mod.decode_png(compiler.textures[latch][0])[2]
                self.assertEqual(tier_rgba[clear:clear+4],bytes(4))
        self.assertEqual(compiler.face_texture_pixel_count,18*4096)
        self.compile('west','single','wood',compiler)
        self.assertEqual(compiler.face_texture_pixel_count,18*4096)
        compiler=self.compiler();compiler.face_texture_pixel_count=chest.MAX_GENERATED_PIXELS
        with self.assertRaisesRegex(mod.ImportError,'budget'):
            self.compile(compiler=compiler)

    def test_all_blocks_full_partial_selectors_and_nested_guard(self):
        compiler=self.compiler()
        for block in chest.TIERS:
            rows=compiler.block(block,compiler.assets.json(mod.asset_path(block,'blockstates')))
            self.assertEqual(len(rows),20)
            self.assertFalse(compiler.unsupported_states)
        composite={'loader':'neoforge:composite','children':{'base':{'parent':'sophisticatedstorage:block/chest'}}}
        with self.assertRaisesRegex(mod.ImportError,'explicit'):
            compiler.compile_model({'model':composite},block_name='sophisticatedstorage:chest')
        state={'facing':'east','type':'left'}
        result=compiler.compile_model({'model':composite},selector=state,block_name='sophisticatedstorage:chest')
        self.assertTrue(result['fixed_rotation'])
        for app in ({'y':90},{'model':{**composite,'transform':{'translation':[1,0,0]}}}):
            with self.assertRaisesRegex(mod.ImportError,'transform|rotation'):
                compiler.compile_model({'model':composite,**app},selector=state,block_name='sophisticatedstorage:chest')

    def test_resource_overrides_and_material_inheritance(self):
        compiler=self.compiler();materials=copy.deepcopy(chest._materials(compiler,mod.ImportError))
        self.assertEqual(materials['single']['base'],'fixturechest:single_base')
        self.assertEqual(materials['left']['copper_tier'],'fixturechest:left_copper')
        self.compile(compiler=compiler)
        self.assertEqual(materials,chest._materials(compiler,mod.ImportError))
        self.put('assets/fixturechest/textures/single_base.png',mod.encode_png(128,128,bytes((5,6,7,255))*128*128))
        compiler,model=self.compile()
        w,h,rgba=mod.decode_png(compiler.textures[model['polygons'][0]['texture']][0])
        self.assertEqual((w,h),(128,128));self.assertEqual(rgba[(40*128+44)*4:(40*128+44)*4+4],bytes((5,6,7,255)))

    def test_archive_material_definitions_and_loose_override(self):
        sources=[path for path in self.root.rglob('*') if path.is_file()]
        archive=self.root/'fixture.jar'
        with zipfile.ZipFile(archive,'w') as output:
            for path in sources:output.write(path,path.relative_to(self.root).as_posix())
        assets=mod.Assets([archive]);self.addCleanup(assets.close)
        compiler=mod.Compiler(assets)
        _,result=self.compile('south','right','copper',compiler)
        self.assertEqual(len(result['polygons']),18)
        self.assertEqual(chest._materials(compiler,mod.ImportError)['right']['base'],'fixturechest:right_base')
        override=self.root/'override'
        path=override/'assets/sophisticatedstorage/storage_texture_definitions/acacia_chest.json'
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({'parent':'fixturechest:chest','textures':{
            'single':{'base':'fixturechest:left_base'},'left':{'base':'fixturechest:left_base'},
            'right':{'base':'fixturechest:right_base'}}}))
        assets=mod.Assets([archive,override]);self.addCleanup(assets.close)
        compiler=mod.Compiler(assets)
        self.assertEqual(chest._materials(compiler,mod.ImportError)['single']['base'],'fixturechest:left_base')
        self.compile(compiler=compiler)

    def test_invalid_materials_and_fractional_alpha(self):
        path='assets/sophisticatedstorage/storage_texture_definitions/acacia_chest.json'
        original=self.compiler().assets.json(path)
        for bad in ({'parent':'sophisticatedstorage:acacia_chest'}, {'parent':'fixturechest:absent'},
                    {'type':'other'}, {'type':'chest'}, {'type':'chest','textures':{'single':{'base':'../bad'}}}):
            self.put(path,bad)
            with self.assertRaises(mod.ImportError):self.compile()
        self.put(path,original)
        for texture in ('single_base','single_wood'):
            self.put('assets/fixturechest/textures/'+texture+'.png',mod.encode_png(64,64,bytes((1,2,3,127))*4096))
            with self.assertRaisesRegex(mod.ImportError,'binary alpha'):self.compile()
            chest_assets(self.root,mod.encode_png)
        self.put('assets/other/storage_texture_definitions/acacia_chest.json',original)
        with self.assertRaisesRegex(mod.ImportError,'unambiguous'):self.compile()

    def test_invalid_states_expansion_bounds_and_replacement_model(self):
        for facing,shape in [('up','single'),('north','both')]:
            with self.assertRaises(mod.ImportError):self.compile(facing,shape)
        compiler=self.compiler()
        definition={'variants':{f'slot={i}':{'model':'sophisticatedstorage:block/chest'} for i in range(205)}}
        with self.assertRaisesRegex(mod.ImportError,'4096'):
            chest.expand_chest(compiler,'sophisticatedstorage:chest',definition,mod.ImportError)
        # Explicit replacement JSON must keep the normal compiler path.
        self.put('assets/sophisticatedstorage/models/block/chest.json',
                 {'textures':{'x':'fixturechest:single_base'},'elements':[{'from':[0,0,0],'to':[1,1,1],'faces':{'up':{'texture':'#x'}}}]})
        compiler=self.compiler();rows=compiler.block('sophisticatedstorage:chest',{'variants':{'':{'model':'sophisticatedstorage:block/chest'}}})
        self.assertEqual(len(rows),1);self.assertNotIn('polygons',rows[0][3][0])


if __name__=='__main__':unittest.main()
