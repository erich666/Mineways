"""Twilight chest BER identity, source mesh reuse, palette and texture boundaries."""
import json
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod
from mod_twilight_chest_fixtures import FAMILIES, twilight_chest_assets

chest = mod._chest_module


class TwilightChests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        twilight_chest_assets(self.root,mod.encode_png)

    def put(self,path,value):
        destination=self.root/path
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(value if isinstance(value,bytes) else json.dumps(value).encode())

    def compiler(self):
        assets=mod.Assets([self.root]);self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self,compiler=None,block='twilightforest:canopy_chest',state=None,**application):
        compiler=compiler or self.compiler()
        return compiler.compile_model({'model':block.replace(':',':block/'),**application},
                                      selector=state,block_name=block)

    def test_all_sixteen_blocks_full_and_partial_selectors(self):
        compiler=self.compiler()
        for block in chest.TWILIGHT_CHESTS:
            rows=compiler.block(block,compiler.assets.json(mod.asset_path(block,'blockstates')))
            self.assertEqual(len(rows),20)
            self.assertFalse(compiler.unsupported_states)
            for _,_,_,models in rows:
                self.assertEqual(len(models[0]['polygons']),18)
        self.assertEqual(len(compiler.textures),48)
        self.assertEqual(compiler.element_count,320*18)

    def test_source_json_cannot_forge_or_replace_internal_entity_marker(self):
        path = 'assets/twilightforest/models/block/canopy_chest.json'
        for value in (True, False):
            forged = {'_builtin_entity': value}
            self.put(path, forged)
            with self.assertRaises(mod.ImportError):
                self.compile()
            with self.assertRaises(mod.ImportError):
                self.compile(model=forged)
            self.put(path, {'parent': 'minecraft:builtin/entity', **forged})
            self.assertEqual(len(self.compile()['polygons']), 18)
        self.put('assets/minecraft/models/builtin/entity.json', {'_builtin_entity': True})
        with self.assertRaises(mod.ImportError):
            self.compile()

    def test_exact_cube_contract_all_shapes_and_facings(self):
        compiler=self.compiler()
        for shape,(x,w,lx,lw) in {'single':(1,14,7,2),'right':(1,15,15,1),'left':(0,15,0,1)}.items():
            for facing in chest.FACINGS:
                result=self.compile(compiler,state={'facing':facing,'type':shape,'waterlogged':'true'})
                polygons=result['polygons']
                self.assertTrue(result['fixed_rotation'])
                self.assertEqual(result['rotation'],(0,0,0))
                self.assertEqual(len({p['texture'] for p in polygons}),1)
                for offset,lo,hi in ((0,(x,0,1),(x+w,10,15)),(6,(x,9,1),(x+w,14,15)),
                                     (12,(lx,7,15),(lx+lw,11,16))):
                    points=[v for p in polygons[offset:offset+6] for v in p['vertices']]
                    corners=[chest._orient(p,facing) for p in (lo,hi)]
                    self.assertEqual(tuple(min(v[d] for v in points) for d in range(3)),
                                     tuple(min(v[d] for v in corners) for d in range(3)))
                    self.assertEqual(tuple(max(v[d] for v in points) for d in range(3)),
                                     tuple(max(v[d] for v in corners) for d in range(3)))
                # Literal vanilla DOWN rectangle for the bottom cube.
                self.assertEqual([v[3:] for v in polygons[0]['vertices']],
                                 [((14+w)/4,19/4),(14/4,19/4),(14/4,33/4),((14+w)/4,33/4)])
        expected=self.compile(compiler,state={'facing':'north','type':'single'})
        self.assertEqual(self.compile(compiler),expected)
        self.assertEqual(self.compile(compiler,x=90,y=270,**{'ae2:z':90,'uvlock':True}),expected)

    def test_full_texture_including_latch_and_trapped_mapping(self):
        compiler=self.compiler()
        for index,(wood,material) in enumerate(FAMILIES):
            for trapped in (False,True):
                block='twilightforest:'+wood+('_trapped_chest' if trapped else '_chest')
                for shape_index,shape in enumerate(chest.TYPES):
                    result=self.compile(compiler,block,state={'type':shape})
                    texture=result['polygons'][0]['texture']
                    self.assertTrue(all(p['texture']==texture for p in result['polygons']))
                    w,h,pixels=mod.decode_png(compiler.textures[texture][0])
                    self.assertEqual((w,h),(64,64))
                    offset=(17*64+23)*4
                    self.assertEqual(tuple(pixels[offset:offset+4]),(92,68,20+24*index+3*shape_index+12*trapped,255))
        self.assertEqual(compiler.face_texture_pixel_count,0)
        self.assertEqual(len(compiler._twilight_chest_checked_textures),48)

    def test_resource_overrides_preserved_and_missing_texture_rejects(self):
        path='assets/twilightforest/textures/entity/chest/canopy/single.png'
        self.put(path,mod.encode_png(128,128,bytes((2,4,8,255))*128*128))
        compiler=self.compiler();result=self.compile(compiler)
        self.assertEqual(mod.decode_png(compiler.textures[result['polygons'][0]['texture']][0])[:2],(128,128))
        (self.root/path).unlink()
        with self.assertRaisesRegex(mod.ImportError,'missing texture'):self.compile()

    def test_alpha_boundary_and_success_cache(self):
        path='assets/twilightforest/textures/entity/chest/canopy/single.png'
        self.put(path,mod.encode_png(64,64,bytes((2,4,8,127))*4096))
        with self.assertRaisesRegex(mod.ImportError,'binary alpha'):self.compile()
        self.put(path,mod.encode_png(64,64,bytes((2,4,8,0))*4096))
        compiler=self.compiler();self.compile(compiler);self.compile(compiler,state={'facing':'east'})
        self.assertEqual(len(compiler._twilight_chest_checked_textures),1)

    def test_identity_sentinel_and_explicit_visible_override(self):
        compiler=self.compiler()
        model=compiler.model('twilightforest:block/canopy_chest')
        self.assertFalse(chest.is_twilight_chest('fixture:chest',model))
        self.assertFalse(chest.is_twilight_chest('twilightforest:canopy_chest',{'parent':'minecraft:builtin/entity'}))
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model':'twilightforest:block/canopy_chest'},block_name='fixture:chest')
        visible={'parent':'minecraft:builtin/entity','textures':{'all':'twilightforest:entity/chest/canopy/single'},
                 'elements':[{'from':[2,3,4],'to':[5,6,7],'faces':{'up':{'texture':'#all'}}}]}
        self.put('assets/twilightforest/models/block/canopy_chest.json',visible)
        compiler=self.compiler();rows=compiler.block('twilightforest:canopy_chest',{'variants':{'':{'model':'twilightforest:block/canopy_chest'}}})
        self.assertEqual(len(rows),1);self.assertEqual(len(rows[0][3][0]['elements']),1)
        self.assertNotIn('polygons',rows[0][3][0])
        # A supplied engine-parent replacement also remains an ordinary model.
        self.put('assets/twilightforest/models/block/canopy_chest.json',{'parent':'minecraft:builtin/entity'})
        visible.pop('parent');self.put('assets/minecraft/models/builtin/entity.json',visible)
        compiler=self.compiler();self.assertNotIn('polygons',self.compile(compiler))

    def test_nested_guards_and_state_validation(self):
        compiler=self.compiler();block='twilightforest:canopy_chest'
        composite={'loader':'neoforge:composite','children':{'chest':{'parent':'twilightforest:block/canopy_chest'}}}
        with self.assertRaisesRegex(mod.ImportError,'explicit facing'):
            compiler.compile_model({'model':composite},block_name=block)
        state={'facing':'east','type':'left'}
        result=compiler.compile_model({'model':composite},selector=state,block_name=block)
        self.assertTrue(result['fixed_rotation'])
        with self.assertRaisesRegex(mod.ImportError,'rotation|transform'):
            compiler.compile_model({'model':composite,'y':90},selector=state,block_name=block)
        for state in ({'facing':'up'},{'type':'both'}):
            with self.assertRaisesRegex(mod.ImportError,'invalid chest'):self.compile(state=state)
        crowded={'variants':{f'slot={i}':{'model':'twilightforest:block/canopy_chest'} for i in range(205)}}
        with self.assertRaisesRegex(mod.ImportError,'4096'):chest.expand_chest(compiler,block,crowded,mod.ImportError)

    def test_shared_polygon_budget(self):
        compiler=self.compiler();compiler.element_count=chest.MAX_COMPILED_ELEMENTS-18
        self.compile(compiler);self.assertEqual(compiler.element_count,chest.MAX_COMPILED_ELEMENTS)
        compiler=self.compiler();compiler.element_count=chest.MAX_COMPILED_ELEMENTS-17
        with self.assertRaisesRegex(mod.ImportError,'budget'):self.compile(compiler)
        self.assertEqual(compiler.textures,{})
        self.assertEqual(compiler.element_count,chest.MAX_COMPILED_ELEMENTS-17)


if __name__=='__main__':unittest.main()
