"""Twilight vanilla sign bodies preserve registered wood, geometry and palette."""
import math
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_twilight_sign_fixtures import WOODS, twilight_sign_assets


class TwilightSigns(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name);twilight_sign_assets(self.root,mod.encode_png)

    def compiler(self,sources=None):
        assets=mod.Assets(sources or [self.root]);self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self,compiler=None,block='twilightforest:canopy_sign',state=None,**app):
        return (compiler or self.compiler()).compile_model({'model':block.replace(':',':block/'),**app},
                                                          selector=state,block_name=block)

    def test_all_sixteen_blocks_expansion_and_materials(self):
        compiler=self.compiler();count=0
        for index,wood in enumerate(WOODS):
            for wall in (False,True):
                name=(('twilight' if wood=='twilight_oak' else wood)+'_wall_sign') if wall else wood+'_sign'
                block='twilightforest:'+name
                rows=compiler.block(block,compiler.assets.json(mod.asset_path(block,'blockstates')))
                self.assertEqual(len(rows),5 if wall else 17);count+=len(rows)
                self.assertFalse(compiler.unsupported_states)
                for row in rows:
                    faces=row[3][0]['polygons'];self.assertEqual(len(faces),6 if wall else 12)
                    self.assertEqual({f['texture'] for f in faces},{'twilightforest:entity/signs/'+wood})
                pixels=mod.decode_png(compiler.textures['twilightforest:entity/signs/'+wood][0])[2]
                offset=(22*64+19)*4
                self.assertEqual(tuple(pixels[offset:offset+4]),(76,88,30+24*index,255))
        self.assertEqual(count,176);self.assertEqual(len(compiler.textures),8)

    def test_literal_mesh_and_all_heading_turns(self):
        model=self.compile();s=0.6666666865348816
        self.assertEqual(model['rotation'],(0,0,0));self.assertTrue(model['fixed_rotation'])
        face=model['polygons'][0]['vertices']
        self.assertEqual(face,[(8+12*s,8+14*s,8-s,6.5,0),(8-12*s,8+14*s,8-s,.5,0),
                               (8-12*s,8+14*s,8+s,.5,1),(8+12*s,8+14*s,8+s,6.5,1)])
        self.assertEqual(self.compile(state={'rotation':'0'}),model)
        for turn in range(16):
            actual=self.compile(state={'rotation':str(turn)})['polygons']
            c,sine=math.cos(math.radians(22.5*turn)),math.sin(math.radians(22.5*turn))
            for source,output in zip(model['polygons'],actual):
                for a,b in zip(source['vertices'],output['vertices']):
                    self.assertAlmostEqual(b[0],8+c*(a[0]-8)-sine*(a[2]-8))
                    self.assertAlmostEqual(b[2],8+sine*(a[0]-8)+c*(a[2]-8))
                    self.assertEqual(a[1],b[1]);self.assertEqual(a[3:],b[3:])
        self.assertEqual(self.compile(x=90,y=270,uvlock=True,**{'ae2:z':90}),model)

    def test_wall_source_offset_and_default(self):
        block='twilightforest:canopy_wall_sign';s=0.6666666865348816
        self.assertEqual(self.compile(block=block),self.compile(block=block,state={'facing':'north'}))
        bounds={'north':((8-12*s,3+2*s,15-s),(8+12*s,3+14*s,15+s)),
                'south':((8-12*s,3+2*s,1-s),(8+12*s,3+14*s,1+s)),
                'east':((1-s,3+2*s,8-12*s),(1+s,3+14*s,8+12*s)),
                'west':((15-s,3+2*s,8-12*s),(15+s,3+14*s,8+12*s))}
        for facing,(low,high) in bounds.items():
            faces=self.compile(block=block,state={'facing':facing})['polygons'];self.assertEqual(len(faces),6)
            points=[v for f in faces for v in f['vertices']]
            for axis in range(3):
                self.assertAlmostEqual(min(v[axis] for v in points),low[axis])
                self.assertAlmostEqual(max(v[axis] for v in points),high[axis])

    def test_trusted_sentinel_and_visible_overrides(self):
        compiler=self.compiler();block='twilightforest:canopy_sign'
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model':'twilightforest:block/canopy_sign'},block_name='fixture:sign')
        path='assets/twilightforest/models/block/canopy_sign.json'
        put(self.root,path,{'_builtin_entity':True})
        with self.assertRaises(mod.ImportError):self.compile()
        put(self.root,path,{'parent':'minecraft:builtin/entity','elements':[]})
        with self.assertRaises(mod.ImportError):self.compile()
        visible={'parent':'minecraft:builtin/entity','textures':{'body':'twilightforest:entity/signs/canopy'},
                 'elements':[{'from':[1,2,3],'to':[4,5,6],'faces':{'up':{'texture':'#body'}}}]}
        put(self.root,path,visible)
        compiler=self.compiler();result=self.compile(compiler)
        self.assertNotIn('polygons',result);self.assertEqual(result['elements'][0]['coordinates'][:3],(1,2,3))
        self.assertEqual(len(compiler.block(block,{'variants':{'':{'model':'twilightforest:block/canopy_sign'}}})),1)
        visible.pop('parent');put(self.root,'assets/minecraft/models/builtin/entity.json',visible)
        put(self.root,path,{'parent':'minecraft:builtin/entity'})
        self.assertNotIn('polygons',self.compile())

    def test_texture_replacement_and_alpha_boundary(self):
        path='assets/twilightforest/textures/entity/signs/canopy.png'
        put(self.root,path,mod.encode_png(128,64,bytes((7,14,21,255))*128*64))
        compiler=self.compiler();texture=self.compile(compiler)['polygons'][0]['texture']
        self.assertEqual(mod.decode_png(compiler.textures[texture][0]),(128,128,bytes((7,14,21,255))*128*128))
        put(self.root,path,mod.encode_png(2,1,bytes((7,14,21,127))*2))
        with self.assertRaisesRegex(mod.ImportError,'binary alpha'):self.compile()
        (self.root/path).unlink()
        with self.assertRaisesRegex(mod.ImportError,'missing texture'):self.compile()

    def test_nested_orientation_and_transform_guards(self):
        compiler=self.compiler();block='twilightforest:canopy_sign'
        outer={'loader':'neoforge:composite','children':{'sign':{'parent':'twilightforest:block/canopy_sign'}}}
        with self.assertRaisesRegex(mod.ImportError,'explicit palette'):
            compiler.compile_model({'model':outer},block_name=block)
        result=compiler.compile_model({'model':outer},selector={'rotation':'3'},block_name=block)
        self.assertTrue(result['fixed_rotation'])
        for app in ({'y':90},{'model':{**outer,'transform':{'translation':[1,0,0]}}}):
            with self.assertRaisesRegex(mod.ImportError,'rotation|transform'):
                compiler.compile_model({'model':outer,**app},selector={'rotation':'3'},block_name=block)

    def test_bad_states_preserved_properties_and_budget(self):
        for state in ({'rotation':'16'},{'rotation':'01'},{'rotation':'-1'}):
            with self.assertRaises(mod.ImportError):self.compile(state=state)
        with self.assertRaises(mod.ImportError):self.compile(block='twilightforest:canopy_wall_sign',state={'facing':'up'})
        definition={'variants':{'waterlogged=true':{'model':'twilightforest:block/canopy_sign'}}}
        compiler=self.compiler();rows=compiler.block('twilightforest:canopy_sign',definition)
        self.assertEqual(len(rows),17);self.assertTrue(all('waterlogged=true' in row[0] for row in rows))
        compiler=self.compiler();compiler.element_count=mod.MAX_COMPILED_ELEMENTS-12
        self.compile(compiler);self.assertEqual(compiler.element_count,mod.MAX_COMPILED_ELEMENTS)
        compiler=self.compiler();compiler.element_count=mod.MAX_COMPILED_ELEMENTS-11
        with self.assertRaisesRegex(mod.ImportError,'budget'):
            compiler.block('twilightforest:canopy_sign',definition)
        self.assertEqual(compiler.textures,{})
        self.assertEqual(compiler.element_count,mod.MAX_COMPILED_ELEMENTS-11)
        compiler=self.compiler();definition={'variants':{f'slot={i}':{'model':'twilightforest:block/canopy_sign'} for i in range(241)}}
        with self.assertRaisesRegex(mod.ImportError,'4096'):
            compiler.block('twilightforest:canopy_sign',definition)

    def test_hanging_all_identities_selector_counts_and_materials(self):
        compiler=self.compiler();total=0
        for index,wood in enumerate(WOODS):
            for wall in (False,True):
                block='twilightforest:'+wood+('_wall_hanging_sign' if wall else '_hanging_sign')
                rows=compiler.block(block,compiler.assets.json(mod.asset_path(block,'blockstates')))
                self.assertEqual(len(rows),5 if wall else 51);total+=len(rows)
                self.assertFalse(compiler.unsupported_states)
                for row in rows:
                    count=20 if wall else 8 if 'attached=true' in row[0] else 14
                    self.assertEqual(len(row[3][0]['polygons']),count)
                texture='twilightforest:entity/signs/hanging/'+wood
                pixels=mod.decode_png(compiler.textures[texture][0])[2]
                offset=(22*64+19)*4
                self.assertEqual(tuple(pixels[offset:offset+4]),(76,88,40+24*index,255))
        self.assertEqual(total,448);self.assertEqual(compiler.element_count,5696)
        self.assertEqual(len(compiler.textures),8)

    def test_hanging_source_board_chains_and_partial_states(self):
        block='twilightforest:twilight_oak_hanging_sign'
        default=self.compile(block=block)
        self.assertEqual(default,self.compile(block=block,state={'rotation':'0','attached':'false'}))
        self.assertEqual(default['polygons'][0]['vertices'],[(15,10,7,4,6),(1,10,7,.5,6),
                                                           (1,10,9,.5,7),(15,10,9,4,7)])
        attached=self.compile(block=block,state={'attached':'true'})
        self.assertEqual(len(attached['polygons']),8)
        self.assertEqual([v[3:] for v in attached['polygons'][-2]['vertices']],[(6.5,3),(3.5,3),(3.5,6),(6.5,6)])
        self.assertEqual([v[3:] for v in attached['polygons'][-1]['vertices']],[(9.5,3),(6.5,3),(6.5,6),(9.5,6)])
        # Both sides share a plane but retain separate texture ranges.
        self.assertEqual({v[:3] for v in attached['polygons'][-2]['vertices']},
                         {v[:3] for v in attached['polygons'][-1]['vertices']})
        self.assertEqual(attached,self.compile(block=block,state={'rotation':'0','attached':'true'}))
        self.assertEqual(default,self.compile(block=block,x=90,y=270,uvlock=True,**{'ae2:z':90}))
        compiler=self.compiler();application={'model':block.replace(':',':block/')}
        for state,count in [('rotation=7',3),('attached=true',17),('rotation=7,attached=true',1)]:
            rows=compiler.block(block,{'variants':{state:application}})
            self.assertEqual(len(rows),count)
            self.assertTrue(all(all(prop in row[0] for prop in state.split(',')) for row in rows))
        wall=self.compile(block='twilightforest:twilight_oak_wall_hanging_sign')
        self.assertEqual(len(wall['polygons']),20)
        points=[v for f in wall['polygons'] for v in f['vertices']]
        for axis,(low,high) in enumerate(((0,16),(0,16),(6,10))):
            self.assertAlmostEqual(min(v[axis] for v in points),low)
            self.assertAlmostEqual(max(v[axis] for v in points),high)

    def test_hanging_override_guards_and_exact_budgets(self):
        block='twilightforest:canopy_hanging_sign';compiler=self.compiler()
        application={'model':block.replace(':',':block/')}
        for state in ({},{'rotation':'0'},{'attached':'true'}):
            with self.assertRaisesRegex(mod.ImportError,'explicit palette'):
                compiler.compile_model(application,depth=1,selector=state,block_name=block)
        with self.assertRaises(mod.ImportError):self.compile(block=block,state={'attached':'yes'})
        for state,count in [({'attached':'true'},8),({'attached':'false'},14)]:
            compiler=self.compiler();compiler.element_count=mod.MAX_COMPILED_ELEMENTS-count
            self.compile(compiler,block,state);self.assertEqual(compiler.element_count,mod.MAX_COMPILED_ELEMENTS)
            compiler=self.compiler();compiler.element_count=mod.MAX_COMPILED_ELEMENTS-count+1
            with self.assertRaisesRegex(mod.ImportError,'budget'):self.compile(compiler,block,state)
            self.assertEqual(compiler.textures,{})
        put(self.root,'assets/twilightforest/models/block/canopy_hanging_sign.json',{
            'parent':'minecraft:builtin/entity','textures':{'body':'twilightforest:entity/signs/hanging/canopy'},
            'elements':[{'from':[0,0,0],'to':[1,1,1],'faces':{'up':{'texture':'#body'}}}]})
        compiler=self.compiler();rows=compiler.block(block,{'variants':{'':application}})
        self.assertEqual(len(rows),1);self.assertNotIn('polygons',rows[0][3][0])

    def test_hanging_unsorted_extra_properties_do_not_duplicate_selector(self):
        from mod_hanging_canvas_fixtures import hanging_canvas_sign_assets
        hanging_canvas_sign_assets(self.root,mod.encode_png)
        for block,model in (('twilightforest:canopy_hanging_sign','twilightforest:block/canopy_hanging_sign'),
                            ('farmersdelight:hanging_canvas_sign','farmersdelight:block/canvas_sign')):
            compiler=self.compiler()
            rows=compiler.block(block,{'variants':{'waterlogged=true,custom=kept':{'model':model}}})
            self.assertEqual(len(rows),51)
            self.assertEqual(len({row[0] for row in rows}),51)
            self.assertTrue(all('custom=kept' in row[0] and 'waterlogged=true' in row[0] for row in rows))


if __name__=='__main__':unittest.main()
