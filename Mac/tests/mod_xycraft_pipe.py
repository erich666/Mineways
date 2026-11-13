"""Exact XyCraft missing-data core/cap selection and source scope regressions."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod
from mod_xycraft_pipe_fixtures import xycraft_pipe_assets


class XycraftPipe(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.wrapper, self.core, self.cap = xycraft_pipe_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def put(self, name, model):
        path = self.root / mod.asset_path(name, 'models')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(model))

    def compile(self, compiler=None, model='fixture:xycraft_pipe', **turn):
        return (compiler or self.compiler()).compile_model({'model': model, **turn})

    def test_seven_parts_exact_source_and_unculled_faces(self):
        compiler = self.compiler()
        source = copy.deepcopy(compiler.model('fixture:xycraft_cap'))
        parts = self.compile(compiler)['split_models']
        self.assertEqual(len(parts), 7)
        self.assertEqual(parts[0]['elements'][0]['coordinates'][:6], (6,6,5,10,11,10))
        for part in parts[1:]:
            self.assertEqual(part['elements'][0]['coordinates'][:6], (3,3,5,11,5,13))
            self.assertEqual(part['elements'][0]['faces'][0][:6], ('down',1,2,14,13,90))
            self.assertEqual(part['elements'][1]['coordinates'][6:], ('z',22.5,5,4,7,1))
        for part in parts:
            for element in part['elements']:
                self.assertTrue(all(face[-1] == '-' for face in element['faces']))
        self.assertEqual(compiler.model('fixture:xycraft_cap'), source)
        texture = compiler.textures['fixture:xycraft_pipe'][0]
        self.assertEqual(mod.decode_png(texture), mod.decode_png((self.root/'assets/fixture/textures/xycraft_pipe.png').read_bytes()))
        self.assertTrue(any('cache-zero' in w for w in compiler.warnings))

    def test_cap_axes_and_application_independence(self):
        # Independent axes from Direction.getOpposite().getRotation's JOML
        # Rx(+90)*Rz(...) matrices. Keep full bases, not just face normals.
        expected = [((1,0,0),(0,1,0),(0,0,1)),
                    ((1,0,0),(0,-1,0),(0,0,-1)),
                    ((1,0,0),(0,0,1),(0,-1,0)),
                    ((-1,0,0),(0,0,-1),(0,-1,0)),
                    ((0,0,-1),(1,0,0),(0,-1,0)),
                    ((0,0,1),(-1,0,0),(0,-1,0))]
        for application in ({}, {'x':90,'y':270,'uvlock':True}, {'ae2:z':90}):
            parts = self.compile(**application)['split_models']
            self.assertEqual(parts[0]['rotation'], (90,270,1) if 'x' in application else (0,0,0,90) if application else (0,0,0))
            for part, basis in zip(parts[1:], expected):
                r = part['rotation']
                self.assertEqual(r[2], 0)
                self.assertEqual(mod.quarter_turn_orientation(r[0],r[1],r[3] if len(r)>3 else 0), basis)

    def test_obj_composite_visibility_and_uvs(self):
        compiler = self.compiler()
        parts = self.compile(compiler, model='fixture:xycraft_pipe_obj')['split_models']
        for cap in parts[1:]:
            self.assertEqual(cap['elements'], [])
            self.assertEqual(len(cap['polygons']), 1)
            self.assertEqual([v[:3] for v in cap['polygons'][0]['vertices']], [(3,3,5),(11,3,5),(11,3,13),(3,3,13)])
            self.assertEqual([v[3:] for v in cap['polygons'][0]['vertices']], [(1,14),(14,14),(14,3),(1,3)])
        self.assertNotIn('fixture:missing_hidden', compiler.textures)

    def test_required_children_optional_straight_and_missing_dependencies(self):
        for field in ('cap','core','extract','insert','short_straight'):
            for value in (None, [], 'fixture:xycraft_core', {'parent':'fixture:absent'}):
                with self.subTest(field=field,value=value), self.assertRaises(mod.ImportError):
                    self.compile(model={**self.wrapper, field:value})
        source = dict(self.wrapper)
        source.pop('straight')
        self.assertEqual(len(self.compile(model=source)['split_models']), 7)
        with self.assertRaises(mod.ImportError):
            self.compile(model={**source,'straight':None})

    def test_owned_fields_and_independent_child_texture_scope(self):
        child = {'parent':'fixture:xycraft_pipe','cap':{'parent':'fixture:xycraft_core'},
                 'textures':{'cap':'fixture:missing_outer'}}
        parts = self.compile(model=child)['split_models']
        self.assertEqual(parts[1]['elements'][0]['faces'][0][-2], 'fixture:xycraft_pipe')
        with self.assertRaisesRegex(mod.ImportError, 'inline model'):
            self.compile(model={**child,'loader':'xycraft_machines:pipe'})
        own = {**child, **self.wrapper, 'cap':{'parent':'fixture:xycraft_core'}}
        self.assertEqual(self.compile(model=own)['split_models'][1]['elements'][0]['coordinates'][:6], (6,6,5,10,11,10))

    def test_supplied_source_replacement_and_child_transform_boundary(self):
        cap = copy.deepcopy(self.cap)
        cap['elements'][0]['to'][0] = 12
        self.put('fixture:xycraft_cap', cap)
        self.assertEqual(self.compile()['split_models'][1]['elements'][0]['coordinates'][3], 12)
        cap['transform'] = {'translation':[1,0,0]}
        self.put('fixture:xycraft_cap', cap)
        with self.assertRaisesRegex(mod.ImportError, 'child root transform'):
            self.compile()

    def test_cycles_empty_children_and_nested_boundary(self):
        self.put('fixture:cycle', {**self.wrapper,'core':{'parent':'fixture:cycle'}})
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compile(model='fixture:cycle')
        empty = {**self.wrapper,'core':{'elements':[]}}
        with self.assertRaises(mod.ImportError):
            self.compile(model=empty)
        outer = {'loader':'neoforge:composite','children':{'pipe':{'parent':'fixture:xycraft_pipe'}}}
        with self.assertRaisesRegex(mod.ImportError, 'nested split'):
            self.compile(model=outer)

    def test_shared_states_and_catalog_serialization(self):
        destination = self.root/'pack'
        report = mod.compile_pack([self.root], destination)
        self.assertEqual(report['blocks'], 2)
        self.assertEqual(report['selectors'], 4)
        self.assertEqual(report['unsupported'], [])
        self.assertEqual(report['unsupported_states'], [])
        text = (destination/'manifest.mwpack').read_text()
        self.assertEqual(text.count('usemodel\t'), 28)


if __name__ == '__main__':
    unittest.main()
