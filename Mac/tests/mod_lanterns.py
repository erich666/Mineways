"""Wall lanterns include their original body and preserve baked UVs/pivots."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_lantern_fixtures import lantern_assets


class WallLanterns(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-lantern-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.definition, self.body = lantern_assets(self.root, mod.encode_png)

    def compiler(self, paths=None):
        assets = mod.Assets(paths or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, compiler=None, application=None):
        return (compiler or self.compiler()).compile_model(
            application or {'model':'amendments:block/wall_lantern'},
            selector={'facing':'north','attachment':'block'}, block_name='amendments:wall_lantern')

    def test_complete_body_support_all_rotations_preserve_uvs_and_pivots(self):
        compiler = self.compiler()
        original = compiler.compile_model({'model':'minecraft:block/lantern'})
        rows = compiler.block('amendments:wall_lantern', self.definition)
        self.assertEqual(len(rows), 4)
        for name, _, _, models in rows:
            support, body = models
            direction = name.split('facing=')[1].split(']')[0]
            turn = {'north':0,'east':90,'south':180,'west':270}[direction]
            self.assertEqual(support['rotation'], (0,turn,0))
            self.assertEqual(body['rotation'], (0,(turn+270)%360,0))
            self.assertEqual(body['elements'][0]['coordinates'][:6], (1,3,5,9,12,13))
            self.assertEqual(body['elements'][1]['coordinates'], (4,6,8,6,8,10,'z',22.5,5,7,9,1))
            self.assertEqual(sum(len(e['faces']) for m in models for e in m['elements']), 10)
            for before, after in zip(original['elements'], body['elements']):
                self.assertEqual(before['faces'], after['faces'])
        self.assertEqual(compiler.model('minecraft:block/lantern')['elements'], self.body['elements'])
        self.assertTrue(any('static fast-renderer pose' in text for text in compiler.warnings))

    def test_transform_has_independent_expected_corner(self):
        # Native Y270 maps (x,y,z) about (8,8,8) to (z,y,16-x).
        # After the compiled pretranslation, source corner(3,1,5) becomes(5,3,15).
        body = self.compile()['split_models'][1]
        x,y,z = body['elements'][0]['coordinates'][:3]
        self.assertEqual((z,y,16-x), (5,3,15))

    def test_custom_body_priority_and_resource_override(self):
        current = 'assets/amendments/models/block/wall_lantern_lantern.json'
        legacy = 'assets/amendments/models/block/custom_wall_lanterns/lantern.json'
        first = copy.deepcopy(self.body)
        first['elements'][0]['from'][0] = 4
        put(self.root, current, first)
        self.assertEqual(self.compile()['split_models'][1]['elements'][0]['coordinates'][0], 2)
        second = copy.deepcopy(self.body)
        second['elements'][0]['from'][0] = 5
        put(self.root, legacy, second)
        self.assertEqual(self.compile()['split_models'][1]['elements'][0]['coordinates'][0], 3)
        override = self.root / 'override'
        third = copy.deepcopy(self.body)
        third['elements'][0]['from'][0] = 6
        put(override, legacy, third)
        self.assertEqual(self.compile(self.compiler([self.root, override]))['split_models'][1]['elements'][0]['coordinates'][0], 4)

    def test_mod_namespace_custom_body_and_missing_original(self):
        name = 'amendments:forbidden_arcanus/wall_deorum_lantern'
        compiler = self.compiler()
        application = {'model':'amendments:block/wall_lantern'}
        with self.assertRaisesRegex(mod.ImportError, 'missing wall lantern original blockstate: forbidden_arcanus:deorum_lantern'):
            compiler.compile_model(application, block_name=name)
        put(self.root, 'assets/amendments/models/block/custom_wall_lanterns/forbidden_arcanus/deorum_lantern.json', self.body)
        result = self.compiler().compile_model(application, block_name=name)
        self.assertEqual(len(result['split_models'][1]['elements']), 2)
        with self.assertRaisesRegex(mod.ImportError, 'unverified wall lantern original block mapping'):
            self.compiler().compile_model(application, block_name='fixture:wall_lantern')

    def test_missing_body_never_exports_support_alone(self):
        compiler = self.compiler()
        with self.assertRaisesRegex(mod.ImportError, 'missing wall lantern original blockstate: byg:cryptic_lantern'):
            compiler.block('amendments:byg/wall_cryptic_lantern', self.definition)
        self.assertFalse(compiler.textures)
        self.assertEqual(compiler.element_count, 0)

    def test_invalid_support_body_rotations_and_bounds_fail(self):
        for support in (None, 12, []):
            with self.subTest(support=support), self.assertRaisesRegex(mod.ImportError, 'support must be'):
                self.compile(application={'model':{'loader':'amendments:wall_lantern','support':support}})
        with self.assertRaisesRegex(mod.ImportError, 'Y-only'):
            self.compile(application={'model':'amendments:block/wall_lantern','x':90})
        put(self.root, 'assets/minecraft/blockstates/lantern.json', {'variants': {
            'hanging=false': {'model':'minecraft:block/lantern','y':90}}})
        with self.assertRaisesRegex(mod.ImportError, 'unrotated model'):
            self.compile()
        put(self.root, 'assets/amendments/models/block/wall_lantern_lantern.json', {
            'elements':[{'from':[-256,0,0],'to':[-255,1,1],
                         'faces':{'north':{'texture':'fixture:lantern_body','uv':[0,0,16,16]}}}]})
        with self.assertRaisesRegex(mod.ImportError, 'coordinate bounds'):
            self.compile()

    def test_obj_override_preserves_uvs_and_translates_only_positions(self):
        put(self.root, 'assets/fixture/models/lantern.obj', b'mtllib lantern.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0.1 0.2\nvt 0.8 0.2\nvt 0.8 0.9\nusemtl body\nf 1/1 2/2 3/3\n')
        put(self.root, 'assets/fixture/models/lantern.mtl', b'newmtl body\nmap_Kd fixture:lantern_body\n')
        model = {'loader':'neoforge:obj','model':'fixture:models/lantern.obj','flip_v':True}
        put(self.root, 'assets/amendments/models/block/wall_lantern_lantern.json', model)
        compiler = self.compiler()
        source = compiler.compile_model({'model':model})
        body = self.compile(compiler)['split_models'][1]
        for old, new in zip(source['polygons'][0]['vertices'], body['polygons'][0]['vertices']):
            self.assertEqual(new[:3], (old[0]-2,old[1]+2,old[2]))
            self.assertEqual(new[3:], old[3:])

    def test_empty_split_and_unhandled_root_transform_body_fail(self):
        path = 'assets/amendments/models/block/wall_lantern_lantern.json'
        put(self.root, path, {'parent':'ae2:block/matrix_frame'})
        put(self.root, 'assets/ae2/models/block/matrix_frame.json', {})
        # Only the exact matrix frame resource is invisible, so use it as the
        # original blockstate application rather than inventing an empty body.
        (self.root / path).unlink()
        put(self.root, 'assets/minecraft/blockstates/lantern.json', {'variants': {
            'hanging=false': {'model':'ae2:block/matrix_frame'}}})
        with self.assertRaisesRegex(mod.ImportError, 'no visible geometry'):
            self.compile()
        transformed = copy.deepcopy(self.body)
        transformed['transform'] = {'translation':[1,0,0]}
        put(self.root, path, transformed)
        with self.assertRaisesRegex(mod.ImportError, 'unsupported root transform'):
            self.compile()
        put(self.root, path, self.body)
        compiler = self.compiler()
        real_compile = compiler.compile_model
        def split_body(application, *args, **kwargs):
            if application.get('model') == 'amendments:block/wall_lantern_lantern':
                return {'rotation':(0,0,0),'elements':[],'split_models':[]}
            return real_compile(application, *args, **kwargs)
        compiler.compile_model = split_body
        with self.assertRaisesRegex(mod.ImportError, 'without a split provider'):
            self.compile(compiler)

    def test_inherited_loader_keeps_parent_support_and_child_context(self):
        put(self.root, 'assets/fixture/models/inherited_lantern.json', {
            'parent':'amendments:block/wall_lantern',
            'support':'fixture:unavailable_child_support',
            'textures':{'particle':'fixture:lantern_body'}})
        compiler = self.compiler()
        resolved = compiler.model('fixture:inherited_lantern')
        self.assertEqual(resolved['support'], 'fixture:lantern_support')
        self.assertEqual(resolved['textures']['particle'], 'fixture:lantern_body')
        compiled = self.compile(compiler, {'model':'fixture:inherited_lantern'})
        self.assertEqual(len(compiled['split_models'][0]['elements']), 1)

    def test_explicit_loader_uses_own_support_not_parent_support(self):
        put(self.root, 'assets/fixture/models/own_lantern.json', {
            'parent':'amendments:block/wall_lantern','loader':'amendments:wall_lantern'})
        with self.assertRaisesRegex(mod.ImportError, 'support must be'):
            self.compile(application={'model':'fixture:own_lantern'})
        put(self.root, 'assets/fixture/models/own_lantern.json', {
            'parent':'amendments:block/wall_lantern','loader':'amendments:wall_lantern',
            'support':'minecraft:block/lantern'})
        compiled = self.compile(application={'model':'fixture:own_lantern'})
        self.assertEqual(len(compiled['split_models'][0]['elements']), 2)

    def test_nested_composite_does_not_drop_lantern_parts(self):
        application = {'model':{'loader':'neoforge:composite','children': {
            'lantern':{'parent':'amendments:block/wall_lantern'}}}}
        with self.assertRaisesRegex(mod.ImportError, 'nested split model geometry'):
            self.compile(application=application)

    def test_unknown_body_default_properties_and_ambiguous_variant_fail(self):
        for variants in ({'hanging=false,facing=north':{'model':'minecraft:block/lantern'}},
                         {'':{'model':'minecraft:block/lantern'},'hanging=false':{'model':'minecraft:block/lantern'}}):
            put(self.root, 'assets/minecraft/blockstates/lantern.json', {'variants':variants})
            with self.subTest(variants=variants), self.assertRaises(mod.ImportError):
                self.compile()


if __name__ == '__main__':
    unittest.main()
