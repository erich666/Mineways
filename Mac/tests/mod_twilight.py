"""Twilight isolated texture and cardinal-force-field paths, without mod execution."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_twilight_fixtures import twilight_assets


class TwilightModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-twilight-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.connected, self.force = twilight_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, compiler, model, state=None, block='fixture:twilight_connected', **rotation):
        return compiler.compile_model({'model': model, **rotation}, selector=state, block_name=block)

    def test_connected_cube_uvs_and_exact_alpha_composition(self):
        compiler = self.compiler()
        result = self.compile(compiler, 'fixture:twilight_connected', x=90, y=270, uvlock=True)
        self.assertEqual(result['rotation'], (90,270,1))
        self.assertEqual(len(result['elements']), 1)
        element = result['elements'][0]
        self.assertEqual(element['coordinates'][:6], (0,0,0,16,16,16))
        self.assertEqual(len(element['faces']), 6)
        for face in element['faces']:
            self.assertEqual(face[1:5], (0,0,16,16))
        texture = element['faces'][0][-2]
        _, _, pixels = mod.decode_png(compiler.textures[texture][0])
        self.assertEqual(pixels[(3*16+2)*4:(3*16+2)*4+4], bytes((200,160,80,255)))
        self.assertEqual(pixels[(3*16+12)*4:(3*16+12)*4+4], bytes((192,48,40,255)))
        self.assertTrue(any('ConnectionLogic.NONE' in w for w in compiler.warnings))
        self.assertEqual(compiler.face_texture_pixel_count, 256)

    def test_missing_and_cyclic_optional_base_aliases_preserve_overlay(self):
        for aliases in ({'base_texture':'#absent'}, {'base_texture':'#next','next':'#base_texture'},
                        {'base_texture':'minecraft:missingno'}):
            compiler = self.compiler()
            model = {**self.connected, 'textures': {'overlay_texture':'fixture:twilight_overlay', **aliases}}
            result = self.compile(compiler, model)
            self.assertEqual(len(result['elements'][0]['faces']), 6)
            self.assertTrue(all(face[-2] == 'fixture:twilight_overlay' for face in result['elements'][0]['faces']))
            self.assertEqual(set(compiler.textures), {'fixture:twilight_overlay'})
        compiler = self.compiler()
        model = {**self.connected, 'textures': {'overlay_texture':'fixture:twilight_overlay',
                                              'base_texture':'#base', 'base':'fixture:twilight_base'}}
        result = self.compile(compiler, model)
        self.assertTrue(result['elements'][0]['faces'][0][-2].startswith('mineways:twilight/overlay_'))
        model['textures']['base_texture'] = 'fixture:missing_file'
        with self.assertRaisesRegex(mod.ImportError, 'missing texture'):
            self.compile(compiler, model)
        model['textures']['base_texture'] = 5
        with self.assertRaises(mod.ImportError):
            self.compile(compiler, model)

    def test_fixed_door_tints_affect_overlay_only(self):
        for color, expected in [('pink',(200,0,80)), ('blue',(0,160,80)),
                                ('yellow',(200,160,0)), ('violet',(58,0,40))]:
            compiler = self.compiler()
            result = self.compile(compiler, 'fixture:twilight_pink', block='twilightforest:'+color+'_castle_door')
            texture = result['elements'][0]['faces'][0][-2]
            _, _, pixels = mod.decode_png(compiler.textures[texture][0])
            self.assertEqual(tuple(pixels[:3]), expected)
            self.assertEqual(tuple(pixels[12*4:12*4+4]), (192,0,40,255))

    def test_aurora_uses_verified_null_world_color(self):
        compiler = self.compiler()
        model = {'loader': 'twilightforest:connected_texture_block',
                 'textures': {'overlay_texture':'fixture:twilight_overlay'}, 'connected_texture': {'tint_index':0}}
        result = self.compile(compiler, model, block='twilightforest:auroralized_glass')
        texture = result['elements'][0]['faces'][0][-2]
        _, _, pixels = mod.decode_png(compiler.textures[texture][0])
        self.assertEqual(tuple(pixels[:4]), (200*115//255,160*230//255,80*195//255,255))
        self.assertTrue(any('position-dependent' in w for w in compiler.warnings))
        with self.assertRaisesRegex(mod.ImportError, 'no verified block color'):
            self.compile(compiler, model)

    def test_face_mask_keeps_base_and_rejects_entirely_empty_model(self):
        compiler = self.compiler()
        model = {**self.connected, 'connected_texture': {'faces':['up'], 'render_disabled_faces':False}}
        result = self.compile(compiler, model)
        faces = result['elements'][0]['faces']
        self.assertEqual(sum(face[-2] == 'fixture:twilight_base' for face in faces), 5)
        model['textures'] = {'overlay_texture':'fixture:twilight_overlay'}
        result = self.compile(compiler, model)
        self.assertEqual(len(result['elements'][0]['faces']), 1)
        model['connected_texture']['faces'] = []
        with self.assertRaisesRegex(mod.ImportError, 'no visible static'):
            self.compile(compiler, model)

    def test_resource_override_and_owned_geometry_fields(self):
        override = self.root / 'override'
        put(override, 'assets/fixture/textures/twilight_overlay.png', mod.encode_png(1,1,bytes((1,2,3,255))))
        put(self.root, 'assets/fixture/models/child.json', {'parent':'fixture:twilight_connected',
            'connected_texture':{'faces':[], 'render_disabled_faces':False}})
        compiler = self.compiler([self.root, override])
        result = self.compile(compiler, 'fixture:child')
        self.assertEqual(len(result['elements'][0]['faces']), 6)
        texture = result['elements'][0]['faces'][0][-2]
        self.assertEqual(mod.decode_png(compiler.textures[texture][0])[2][:4], bytes((1,2,3,255)))
        explicit = {'parent':'fixture:child', 'loader':'twilightforest:connected_texture_block'}
        self.assertEqual(len(self.compile(compiler, explicit)['elements'][0]['faces']), 6)

    def test_force_field_exact_conditions_bounds_and_uvs(self):
        compiler = self.compiler()
        result = self.compile(compiler, 'fixture:twilight_forcefield', {'north':'true','east':'true'}, y=90, uvlock=True)
        self.assertEqual(result['rotation'], (0,0,0))
        self.assertTrue(result['fixed_rotation'])
        self.assertEqual(len(result['elements']), 2)
        self.assertEqual(result['elements'][0]['coordinates'][:6], (7,6,0,9,10,5))
        self.assertEqual(result['elements'][0]['faces'][0][1:5], (1,4,12,15))
        self.assertEqual(result['elements'][1]['coordinates'][:6], (9,6,5,16,10,11))
        self.assertEqual(result['elements'][1]['faces'][0][1:5], (0,2,15,13))
        self.assertNotIn('fixture:missing_diagonal', compiler.textures)
        self.assertTrue(any('diagonal joins' in w for w in compiler.warnings))

    def test_force_field_expands_cardinal_palette_without_empty_fallback(self):
        compiler = self.compiler()
        definition = compiler.assets.json('assets/fixture/blockstates/twilight_forcefield.json')
        rows = compiler.block('fixture:twilight_forcefield', definition)
        self.assertEqual(len(rows), 127)  # Full assignments and selectors for omitted false flags.
        self.assertTrue(all(models and models[0]['elements'] for _,_,_,models in rows))
        self.assertFalse(compiler.unsupported_states)

    def test_force_field_empty_and_malformed_inputs_fail(self):
        for condition in ({'direction':'bad','parents':[]}, {'direction':'up','parents':'north'},
                          {'direction':'up','parents':[], 'if':1}, []):
            model = copy.deepcopy(self.force)
            model['elements'][0]['condition'] = condition
            with self.subTest(condition=condition), self.assertRaises(mod.ImportError):
                self.compile(self.compiler(), model)
        model = {**self.force, 'elements':[self.force['elements'][-1]]}
        with self.assertRaisesRegex(mod.ImportError, 'no visible static'):
            self.compile(self.compiler(), model, {'north':'true','east':'true'})
        with self.assertRaisesRegex(mod.ImportError, 'true or false'):
            self.compile(self.compiler(), self.force, {'north':'yes'})

    def test_generated_texture_budget_and_failed_state_rollback(self):
        compiler = self.compiler()
        compiler.face_texture_pixel_count = 64 * 1024 * 1024
        with self.assertRaisesRegex(mod.ImportError, 'generated-texture pixel budget'):
            self.compile(compiler, 'fixture:twilight_connected')
        compiler = self.compiler()
        with self.assertRaises(mod.ImportError):
            compiler.block('fixture:failed', {'multipart': [
                {'apply': {'model': 'fixture:twilight_connected'}},
                {'apply': {'model': 'fixture:missing_model'}}]})
        self.assertEqual(compiler.textures, {})
        self.assertEqual(compiler.face_texture_pixels, {})
        self.assertEqual(compiler.face_texture_pixel_count, 0)
        self.assertEqual(compiler.element_count, 0)

    def test_force_field_inherits_owned_elements_but_explicit_loader_resets(self):
        put(self.root, 'assets/fixture/models/force_child.json', {
            'parent': 'fixture:twilight_forcefield', 'elements': [],
            'textures': {'pane':'fixture:twilight_overlay'}})
        compiler = self.compiler()
        result = self.compile(compiler, 'fixture:force_child')
        self.assertEqual(result['elements'][0]['coordinates'][:6], (7,6,5,9,10,11))
        self.assertEqual(result['elements'][0]['faces'][0][-2], 'fixture:twilight_overlay')
        with self.assertRaisesRegex(mod.ImportError, 'bounded supplied elements'):
            self.compile(compiler, {'parent':'fixture:force_child', 'loader':'twilightforest:force_field'})
        model = copy.deepcopy(self.force)
        model['elements'][0]['faces']['north']['cullface'] = 'up'
        result = self.compile(compiler, model)
        self.assertEqual(result['elements'][0]['faces'][0][-1], 'north')

    def test_malformed_connected_options_fail(self):
        for field, value in [('base',[]), ('connected_texture',{'faces':['bad']}),
                             ('connected_texture',{'render_disabled_faces':1}),
                             ('connected_texture',{'tint_index':True}), ('connectable_blocks',['../bad'])]:
            with self.subTest(field=field,value=value), self.assertRaises(mod.ImportError):
                self.compile(self.compiler(), {**self.connected, field:value})
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compile(self.compiler(), {**self.connected, 'transform':{'translation':[1,0,0]}})


if __name__ == '__main__':
    unittest.main()
