"""Custom OBJ geometry is parsed at its loader declaration, separate from context."""
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_obj_ownership_fixtures import obj_ownership_assets
from mod_transmitter_fixtures import transmitter_assets
import mod_ie_obj

OBJ_FIELDS = ('model','automatic_culling','shade_quads','flip_v','emissive_ambient','mtl_override')


class ObjGeometryOwnership(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-obj-owned-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent, self.inherited, self.reset, self.definition = obj_ownership_assets(self.root, mod.encode_png)

    def compiler(self):
        assets = mod.Assets([self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def test_inherited_geometry_ignores_child_fields_but_keeps_context(self):
        compiler = self.compiler()
        model = compiler.model('fixture:obj_owned_inherited')
        self.assertEqual({k:model[k] for k in OBJ_FIELDS}, {k:self.parent[k] for k in OBJ_FIELDS})
        self.assertEqual(model['render_type'], 'minecraft:translucent')
        self.assertEqual(model['visibility'], {'panel':True,'hidden':False})
        result = compiler.compile_model({'model':'fixture:obj_owned_inherited'})
        self.assertEqual(len(result['polygons']), 1)
        face = result['polygons'][0]
        self.assertEqual(face['vertices'], [(16,3,4,2,12),(4,3,4,12,12),(4,13,4,12,2),(16,13,4,2,2)])
        self.assertEqual(face['texture'], 'fixture:obj_owned_inherited')
        self.assertEqual(compiler.model('fixture:obj_owned_parent')['visibility'], self.parent['visibility'])

    def test_explicit_loader_resets_optional_fields_and_uses_own_model(self):
        compiler = self.compiler()
        model = compiler.model('fixture:obj_owned_reset')
        self.assertEqual(model['model'], self.reset['model'])
        self.assertTrue(all(key not in model for key in OBJ_FIELDS[1:]))
        self.assertEqual(model['render_type'], 'minecraft:cutout')
        result = compiler.compile_model({'model':'fixture:obj_owned_reset'})
        self.assertEqual(len(result['polygons']), 1)
        self.assertEqual(result['polygons'][0], {'texture':'fixture:obj_owned_reset','vertices':[
            (3,1,13,2,4),(11,1,13,12,4),(11,9,13,12,14),(3,9,13,2,14)]})

    def test_explicit_loader_requires_own_model_and_valid_own_options(self):
        compiler = self.compiler()
        for own in ({}, {'model':'fixture:models/obj_owned_reset.obj','flip_v':1},
                    {'model':'fixture:models/obj_owned_reset.obj','mtl_override':[]}):
            with self.subTest(own=own), self.assertRaises(mod.ImportError):
                compiler.compile_model({'model':{'parent':'fixture:obj_owned_parent','loader':'neoforge:obj',**own}})

    def test_multilevel_inherited_geometry_keeps_original_owner(self):
        put(self.root, 'assets/fixture/models/obj_owned_grandchild.json', {
            'parent':'fixture:obj_owned_inherited','flip_v':None,'model':17,'mtl_override':[],
            'textures':{'override':'fixture:obj_owned_reset'}})
        compiler = self.compiler()
        output = compiler.compile_model({'model':'fixture:obj_owned_grandchild'})
        self.assertEqual(output['polygons'][0]['texture'], 'fixture:obj_owned_reset')
        self.assertEqual(output['polygons'][0]['vertices'][0], (16,3,4,2,12))
        self.assertEqual(compiler.model('fixture:obj_owned_parent')['flip_v'], True)

    def test_blockstates_preserve_both_original_gradient_textures(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:obj_owned', self.definition)
        self.assertEqual(len(rows),2)
        self.assertFalse(compiler.unsupported_states)
        self.assertEqual(set(compiler.textures), {'fixture:obj_owned_inherited','fixture:obj_owned_reset'})
        for label, expected in [('inherited',(80,112,40,255)),('reset',(40,80,112,255))]:
            width,height,pixels = mod.decode_png(compiler.textures['fixture:obj_owned_'+label][0])
            self.assertEqual((width,height),(16,16))
            self.assertEqual(tuple(pixels[(7*16+5)*4:(7*16+5)*4+4]),expected)

    def test_mekanism_inherited_model_options_and_glass_are_frozen(self):
        parent,_ = transmitter_assets(self.root,mod.encode_png)
        put(self.root, 'assets/fixture/models/owned_mek.json', {
            'parent':'fixture:transmitter','model':'fixture:models/unavailable.obj.mek',
            'flip_v':False,'mtl_override':'missing.mtl','glass':[],
            'textures':{'core':'fixture:obj_owned_reset'},
            'transform':{'translation':[0.125,0,0]}})
        compiler = self.compiler()
        resolved = compiler.model('fixture:owned_mek')
        self.assertEqual(resolved['glass'],parent['glass'])
        self.assertEqual(resolved['model'],parent['model'])
        result = compiler.compile_model({'model':'fixture:owned_mek'})
        self.assertEqual(len(result['polygons']),6)
        self.assertEqual(min(v[0] for face in result['polygons'] for v in face['vertices']),6)
        self.assertEqual(result['polygons'][0]['vertices'][0][3:],(0,12))
        pixels = mod.decode_png(compiler.textures[result['polygons'][0]['texture']][0])[2]
        self.assertEqual(tuple(pixels[(7*16+5)*4:(7*16+5)*4+4]),(20,80,28,255))

    def test_mekanism_explicit_loader_drops_parent_glass_and_flip(self):
        parent,_ = transmitter_assets(self.root,mod.encode_png)
        own={'parent':'fixture:transmitter','loader':'mekanism:transmitter','model':parent['model']}
        compiler=self.compiler()
        resolved=compiler.model(own)
        self.assertNotIn('glass',resolved)
        self.assertNotIn('flip_v',resolved)
        result=compiler.compile_model({'model':own})
        self.assertEqual(result['polygons'][0]['vertices'][0][3:],(0,4))
        for extra in ({'glass':[]},{'flip_v':1}):
            with self.subTest(extra=extra),self.assertRaises(mod.ImportError):
                compiler.compile_model({'model':{**own,**extra}})
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model':{'parent':'fixture:transmitter','loader':'mekanism:transmitter'}})

    def composite_parent(self, loader='neoforge:composite'):
        children = {name:{'elements':[{'from':[offset,0,0],'to':[offset+2,2,2],
                     'faces':{'north':{'texture':'fixture:obj_owned_inherited'}}}]}
                    for name,offset in [('body',1),('support',5)]}
        parent = {'loader':loader,'children':children,'item_render_order':['support','body'],
                  'visibility':{'body':True,'support':True}}
        put(self.root,'assets/fixture/models/owned_composite.json',parent)
        return parent

    def test_composite_inherited_geometry_ignores_child_children_and_item_order(self):
        for loader in ('neoforge:composite','forge:composite'):
            parent=self.composite_parent(loader)
            compiler=self.compiler()
            own={'parent':'fixture:owned_composite','children':{'bogus':{'parent':'fixture:missing'}},
                 'item_render_order':['bogus'],'visibility':{'support':False},
                 'textures':{'particle':'fixture:obj_owned_reset'}}
            model=compiler.model(own)
            self.assertEqual(model['children'],parent['children'])
            self.assertEqual(model['item_render_order'],parent['item_render_order'])
            self.assertEqual(model['visibility'],{'body':True,'support':False})
            self.assertEqual(model['textures']['particle'],'fixture:obj_owned_reset')
            result=compiler.compile_model({'model':own})
            self.assertEqual(len(result['elements']),1)
            self.assertEqual(result['elements'][0]['coordinates'][:3],(1,0,0))

    def test_composite_explicit_loader_uses_only_own_children_and_resets_order(self):
        for loader in ('neoforge:composite','forge:composite'):
            parent=self.composite_parent(loader)
            compiler=self.compiler()
            own={'parent':'fixture:owned_composite','loader':loader,
                 'children':{'new':parent['children']['support']}}
            model=compiler.model(own)
            self.assertEqual(set(model['children']),{'new'})
            self.assertNotIn('item_render_order',model)
            result=compiler.compile_model({'model':own})
            self.assertEqual(len(result['elements']),1)
            self.assertEqual(result['elements'][0]['coordinates'][:3],(5,0,0))
            with self.assertRaisesRegex(mod.ImportError,'composite children'):
                compiler.compile_model({'model':{'parent':'fixture:owned_composite','loader':loader}})

    def test_composite_item_order_does_not_remove_or_duplicate_block_geometry(self):
        parent=self.composite_parent()
        for order in ([],['body'],['support','body','body']):
            compiler=self.compiler()
            result=compiler.compile_model({'model':{**parent,'item_render_order':order}})
            self.assertEqual(len(result['elements']),2)

    def test_composite_item_order_rejects_unknown_names_and_unsupported_shapes(self):
        parent=self.composite_parent()
        for order in (None,{},'body',['absent'],[1],[True],[None],[{}],[['body']]):
            with self.subTest(order=order),self.assertRaisesRegex(mod.ImportError,'item_render_order'):
                self.compiler().compile_model({'model':{**parent,'item_render_order':order}})
        # Child names may look numeric, but this importer supports explicit
        # string references only, without guessing JSON-number lexical forms.
        parent['children']['1']=parent['children']['body']
        result=self.compiler().compile_model({'model':{**parent,'item_render_order':['1']}})
        self.assertEqual(len(result['elements']),3)

    def ie_case(self):
        case=mod_ie_obj.IEObjTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        return case

    def test_ie_inherited_fields_ignore_child_callback_and_model(self):
        case=self.ie_case()
        parent=case.model(['base','diagonal'],'chute',dynamic=True)
        put(case.source,'assets/fixture/models/ie_owned_parent.json',parent)
        compiler=case.compiler()
        own={'parent':'fixture:ie_owned_parent','model':'fixture:models/missing.obj.ie',
             'callbacks':'fixture:unknown','dynamic':[],'layers':[]}
        model=compiler.model(own)
        for key in ('model','callbacks','dynamic','layers'):
            self.assertEqual(model[key],parent[key])
        result=compiler.compile_model({'model':own})
        self.assertEqual(len(result['polygons']),1)

    def test_ie_explicit_loader_resets_callback_dynamic_and_layers(self):
        case=self.ie_case()
        parent=case.model(['base','diagonal'],'chute',dynamic=True,layers=['translucent'])
        put(case.source,'assets/fixture/models/ie_owned_parent.json',parent)
        compiler=case.compiler()
        own={'parent':'fixture:ie_owned_parent','loader':'immersiveengineering:ie_obj','model':parent['model']}
        resolved=compiler.model(own)
        self.assertTrue(all(key not in resolved for key in ('callbacks','dynamic','layers')))
        self.assertEqual(len(compiler.compile_model({'model':own})['polygons']),2)
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model':{'parent':'fixture:ie_owned_parent','loader':'immersiveengineering:ie_obj'}})

    def test_ie_ignores_neoforge_obj_options_but_keeps_material_alias_context(self):
        case=self.ie_case()
        parent=case.model(['base'],'default',textures={'surface':'fixture:white'})
        put(case.source,'assets/fixture/models/test.mtl',b'newmtl surface\nmap_Kd #surface\n')
        compiler=case.compiler()
        expected=compiler.compile_model({'model':parent})
        ignored={'automatic_culling':[], 'shade_quads':'ignored','flip_v':False,
                 'emissive_ambient':None,'mtl_override':'fixture:models/absent.mtl'}
        actual=compiler.compile_model({'model':{**parent,**ignored}})
        self.assertEqual(actual,expected)
        self.assertEqual(actual['polygons'][0]['vertices'][0][3:],(0,16))
        with self.assertRaisesRegex(mod.ImportError,'root transforms/visibility'):
            compiler.compile_model({'model':{**parent,'transform':{'translation':[1,0,0]}}})


if __name__=='__main__':
    unittest.main()
