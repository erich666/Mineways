"""Closed storage shulkers retain bytecode-defined cubes, UVs and palette facing."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_shulker_fixtures import shulker_assets

spec = importlib.util.spec_from_file_location('shulker_adapter', Path(__file__).parents[2] / 'tools/mod_shulker_models.py')
shulker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shulker)


class StorageShulkers(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.model = shulker_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, facing=None, block='sophisticatedstorage:shulker_box', compiler=None,
                model=None, rotation=(0, 0, 0), depth=0):
        compiler = compiler or self.compiler()
        result = shulker.compile_shulker(compiler, 'sophisticatedstorage:block/shulker_box',
            self.model if model is None else model, rotation, {} if facing is None else {'facing': facing},
            depth, mod.ImportError, block, mod.decode_png)
        return compiler, result

    def test_exact_source_uv_rectangles_and_no_head(self):
        _, result = self.compile('up')
        self.assertEqual(len(result['polygons']), 12)
        self.assertEqual(result['elements'], [])
        self.assertTrue(result['fixed_rotation'])
        # Independent sheet rectangles transcribed from ModelPart.Cube.
        rectangles = ((16,28,32,44), (32,44,48,28), (0,44,16,52),
                      (16,44,32,52), (32,44,48,52), (48,44,64,52),
                      (16,0,32,16), (32,16,48,0), (0,16,16,28),
                      (16,16,32,28), (32,16,48,28), (48,16,64,28))
        for face, (u1, v1, u2, v2) in zip(result['polygons'], rectangles):
            self.assertEqual([vertex[3:] for vertex in face['vertices']],
                [(u2/4,v1/4), (u1/4,v1/4), (u1/4,v2/4), (u2/4,v2/4)])
        # Base's source DOWN face becomes the closed body's upper plane at Y=8.
        p = result['polygons'][0]['vertices']
        inset = .004000186920166016
        for vertex, expected in zip(p, ((16-inset,8,inset),(inset,8,inset),
                                         (inset,8,16-inset),(16-inset,8,16-inset))):
            for actual, expected_value in zip(vertex[:3], expected):
                self.assertAlmostEqual(actual, expected_value, places=7)
        low = min(v[1] for face in result['polygons'][6:] for v in face['vertices'])
        self.assertAlmostEqual(low, 4.002000093460083, places=7)

    def test_all_facings_winding_bounds_and_down_default(self):
        _, default = self.compile()
        self.assertEqual(default, self.compile('down')[1])
        self.assertNotEqual(default, self.compile('up')[1])
        # Expected world opening axes are independent of helper rotation formulas.
        axes = {'down':(0,-1,0),'up':(0,1,0),'north':(0,0,-1),
                'south':(0,0,1),'west':(-1,0,0),'east':(1,0,0)}
        for facing, axis in axes.items():
            _, result = self.compile(facing)
            points = [v[:3] for face in result['polygons'] for v in face['vertices']]
            for dimension in range(3):
                self.assertAlmostEqual(min(p[dimension] for p in points), .004000186920166016)
                self.assertAlmostEqual(max(p[dimension] for p in points), 15.995999813079834)
            # The lid's outer source DOWN face points toward the opening axis.
            face = result['polygons'][6]['vertices']
            a, b, c = (v[:3] for v in face[:3])
            ab = [b[i]-a[i] for i in range(3)]; ac = [c[i]-a[i] for i in range(3)]
            normal = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
            self.assertGreater(sum(n*d for n,d in zip(normal,axis)), 0)
            for i in range(0,12,6):
                cube_center = [sum(v[d] for face in result['polygons'][i:i+6] for v in face['vertices'])/24 for d in range(3)]
                for face in result['polygons'][i:i+6]:
                    a,b,c = [v[:3] for v in face['vertices'][:3]]
                    ab=[b[d]-a[d] for d in range(3)];ac=[c[d]-a[d] for d in range(3)]
                    n=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                    self.assertGreater(sum(n[d]*(a[d]-cube_center[d]) for d in range(3)),0)

    def test_all_tier_pixels_and_transparency(self):
        compiler = self.compiler()
        for index, tier in enumerate(('base','copper','iron','gold','diamond','netherite')):
            block = 'sophisticatedstorage:' + (tier+'_' if tier!='base' else '') + 'shulker_box'
            _, result = self.compile('north', block, compiler)
            texture = result['polygons'][0]['texture']
            width, height, rgba = mod.decode_png(compiler.textures[texture][0])
            self.assertEqual((width,height),(64,64))
            marked = (20*64+21)*4
            self.assertEqual(tuple(rgba[marked:marked+4]), (20+index*35,220,180,255) if index else (84,80,60,255))
            other = (20*64+22)*4
            self.assertEqual(tuple(rgba[other:other+4]), (88,80,60,255))
        self.assertEqual(compiler.face_texture_pixel_count,6*4096)
        self.compile('south', 'sophisticatedstorage:copper_shulker_box', compiler)
        self.assertEqual(compiler.face_texture_pixel_count,6*4096)

    def test_resource_override_preserves_pixels_and_standard_model_semantics(self):
        override = self.root/'override'
        put(override,'assets/sophisticatedstorage/textures/entity/shulker_box/no_tint.png',
            mod.encode_png(64,64,bytes((20,40,60,255))*4096))
        compiler,result = self.compile(compiler=self.compiler([self.root,override]))
        texture=result['polygons'][0]['texture']
        self.assertEqual(mod.decode_png(compiler.textures[texture][0])[2],bytes((20,40,60,255))*4096)
        standard={'textures':{'all':'sophisticatedstorage:entity/shulker_box/no_tint'},'elements':[
            {'from':[2,3,4],'to':[5,6,7],'faces':{'north':{'texture':'#all'}}}]}
        put(override,'assets/sophisticatedstorage/models/block/shulker_box.json',standard)
        compiler=self.compiler([self.root,override])
        definition={'variants':{'':{'model':'sophisticatedstorage:block/shulker_box'}}}
        self.assertEqual(shulker.expand_shulker(compiler,'sophisticatedstorage:shulker_box',definition,mod.ImportError),definition)
        rows=compiler.block('sophisticatedstorage:shulker_box',definition)
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0][3][0]['elements'][0]['coordinates'][:6],(2,3,4,5,6,7))

    def test_dispatch_expansion_and_explicit_facing(self):
        compiler=self.compiler()
        definition=compiler.assets.json('assets/sophisticatedstorage/blockstates/shulker_box.json')
        rows=compiler.block('sophisticatedstorage:shulker_box',definition)
        self.assertEqual(len(rows),7)
        self.assertFalse(compiler.unsupported_states)
        indexed={row[0]:row[3][0] for row in rows}
        self.assertEqual(indexed['sophisticatedstorage:shulker_box'],indexed['sophisticatedstorage:shulker_box[facing=down]'])
        explicit={'variants':{'facing=east':{'model':'sophisticatedstorage:block/shulker_box'}}}
        self.assertEqual(shulker.expand_shulker(compiler,'sophisticatedstorage:shulker_box',explicit,mod.ImportError),explicit)
        self.assertEqual(len(compiler.block('sophisticatedstorage:shulker_box',explicit)),1)

    def test_renderer_ignores_json_turns_but_nested_composite_does_not_rotate_it(self):
        compiler, result=self.compile('north',model={**self.model,'transform':{'translation':[3,4,5]},'elements':[]},rotation=(90,270,1,90))
        self.assertEqual(result,self.compile('north')[1])
        self.assertTrue(any('ignored by the shipped' in warning for warning in compiler.warnings))
        composite={'loader':'neoforge:composite','children':{'shulker':self.model}}
        for count in (1,2):
            application={'model':composite}
            with self.assertRaisesRegex(mod.ImportError,'explicit palette flags'):
                self.compiler().compile_model(application,block_name='sophisticatedstorage:shulker_box')
            compiled=self.compiler().compile_model(application,selector={'facing':'west'},block_name='sophisticatedstorage:shulker_box')
            self.assertEqual(compiled['polygons'],self.compile('west')[1]['polygons'])
            with self.assertRaisesRegex(mod.ImportError,'fixed-rotation'):
                self.compiler().compile_model({**application,'y':90},selector={'facing':'west'},block_name='sophisticatedstorage:shulker_box')
            composite={'loader':'neoforge:composite','children':{'nested':composite}}

    def test_nested_composite_root_transforms_cannot_move_fixed_renderer(self):
        transforms = ({'translation':[2,0,0]}, {'rotation':[0,90,0]}, {'scale':[2,1,1]})
        identity = {'translation':[0,0,0], 'rotation':[0,0,0], 'scale':[1,1,1]}
        child = self.model
        for depth in (1,2):
            composite = {'loader':'neoforge:composite', 'children':{'child':child}}
            for transform in transforms:
                with self.subTest(depth=depth,transform=transform), self.assertRaisesRegex(mod.ImportError,'fixed-rotation'):
                    self.compiler().compile_model({'model':{**composite,'transform':transform}},
                        selector={'facing':'up'},block_name='sophisticatedstorage:shulker_box')
            result = self.compiler().compile_model({'model':{**composite,'transform':identity}},
                selector={'facing':'up'},block_name='sophisticatedstorage:shulker_box')
            self.assertEqual(result['polygons'],self.compile('up')[1]['polygons'])
            self.assertTrue(result['fixed_rotation'])
            child = composite

    def test_invalid_missing_and_bounded_inputs(self):
        for facing in ('upward',False,[],''):
            with self.subTest(facing=facing),self.assertRaises(mod.ImportError):self.compile(facing)
        with self.assertRaisesRegex(mod.ImportError,'identity'):self.compile(block='fixture:shulker_box')
        with self.assertRaisesRegex(mod.ImportError,'depth'):self.compile(depth=63)
        compiler=self.compiler();compiler.face_texture_pixel_count=shulker.MAX_GENERATED_PIXELS
        with self.assertRaisesRegex(mod.ImportError,'pixel budget'):self.compile(compiler=compiler)
        (self.root/'assets/sophisticatedstorage/textures/entity/shulker_box/no_tint.png').unlink()
        with self.assertRaises(mod.ImportError):self.compile()

    def test_cutout_textures_reject_fractional_alpha_and_cache_validation(self):
        for source in ('no_tint', 'copper_tier'):
            for alpha in (1, 25, 26, 128, 254):
                override = self.root / 'alpha_override'
                path = f'assets/sophisticatedstorage/textures/entity/shulker_box/{source}.png'
                put(override, path, mod.encode_png(32, 32, bytes((20, 40, 60, alpha)) * 1024))
                compiler = self.compiler([self.root, override])
                with self.subTest(source=source, alpha=alpha), self.assertRaisesRegex(mod.ImportError, 'binary alpha'):
                    self.compile(block='sophisticatedstorage:copper_shulker_box', compiler=compiler)
                (override / path).unlink()
        # Binary replacement images can use a different supported resolution.
        put(override, 'assets/sophisticatedstorage/textures/entity/shulker_box/no_tint.png',
            mod.encode_png(32, 32, bytes((20, 40, 60, 255)) * 1024))
        compiler = self.compiler([self.root, override])
        calls = []
        def decoder(data):
            calls.append(len(data))
            return mod.decode_png(data)
        for facing in ('down', 'up', 'east'):
            result = shulker.compile_shulker(compiler, 'sophisticatedstorage:block/shulker_box',
                self.model, (0, 0, 0), {'facing': facing}, 0, mod.ImportError,
                'sophisticatedstorage:copper_shulker_box', decoder)
            self.assertEqual(len(result['polygons']), 12)
        self.assertEqual(len(calls), 2)  # One check per source for this composed texture.
        texture = result['polygons'][0]['texture']
        width, height, pixels = mod.decode_png(compiler.textures[texture][0])
        self.assertEqual((width, height), (64, 64))
        self.assertEqual(tuple(pixels[:4]), (20, 40, 60, 255))
        self.assertEqual(tuple(pixels[(20*64+21)*4:(20*64+21)*4+4]), (55, 220, 180, 255))

    def test_expansion_rejects_ambiguous_excessive_and_multipart(self):
        compiler=self.compiler(); application={'model':'sophisticatedstorage:block/shulker_box'}
        bad=({'variants':{'facing=sideways':application}},
             {'variants':{f'other={index}':application for index in range(600)}},
             {'variants':{'':application,'facing=up':{**application,'y':90}}},
             {'multipart':[{'apply':application}]})
        for definition in bad:
            with self.subTest(definition=list(definition)),self.assertRaises(mod.ImportError):
                shulker.expand_shulker(compiler,'sophisticatedstorage:shulker_box',definition,mod.ImportError)


if __name__ == '__main__':
    unittest.main()
