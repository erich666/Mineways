"""Pressure tubes preserve supplied geometry and verified local/app rotations."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_pressure_tube_fixtures import pressure_tube_assets


class PressureTubeModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-pressure-tube-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = pressure_tube_assets(self.root, mod.encode_png)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def compile(self, compiler, model='fixture:pressure_tube', **rotation):
        return compiler.compile_model({'model':model, **rotation}, block_name='fixture:pressure_tube')

    def test_six_sources_preserve_geometry_face_uv_rotation_and_pixels(self):
        compiler = self.compiler()
        result = self.compile(compiler)
        parts = result['split_models']
        self.assertEqual(len(parts), 6)
        self.assertEqual([p['rotation'] for p in parts], [(180,0,0),(0,0,0),(90,0,0),(90,180,0),(90,270,0),(90,90,0)])
        for part in parts:
            self.assertEqual(len(part['elements']), 2)
            self.assertEqual(part['elements'][0]['coordinates'][:6], (3,10,5,11,12,13))
            face = part['elements'][0]['faces'][0]
            self.assertEqual(face[:6], ('up',1,2,14,13,90))
            self.assertEqual(part['elements'][1]['faces'][0][:6], ('east',3,4,12,15,0))
            self.assertEqual(part['elements'][1]['coordinates'][6:], ('z',22.5,5,11,7,1))
        self.assertEqual(set(compiler.textures), {'fixture:pressure_tube'})
        actual = compiler.textures['fixture:pressure_tube'][0]
        expected = (self.root/'assets/fixture/textures/pressure_tube.png').read_bytes()
        self.assertEqual(mod.decode_png(actual), mod.decode_png(expected))
        self.assertTrue(any('initialized isolated appearance' in w for w in compiler.warnings))
        self.assertTrue(any('camouflage' in w for w in compiler.warnings))

    def test_composed_orientation_independent_axis_expectations(self):
        compiler = self.compiler()
        plain = self.compile(compiler)['split_models']
        # Apply each rotation to the source up normal. This independently fixes
        # all six target directions, including the west/east sign convention.
        expected = ((0,-1,0),(0,1,0),(0,0,-1),(0,0,1),(-1,0,0),(1,0,0))
        for part, direction in zip(plain, expected):
            r = part['rotation']
            self.assertEqual(mod.quarter_turn_orientation(r[0],r[1],r[3] if len(r)>3 else 0)[1], direction)
        turned = self.compile(compiler, x=90,y=270,uvlock=True)['split_models']
        # Outer X90/Y270 maps (x,y,z) to (-y,z,-x).
        for part, (x,y,z) in zip(turned, expected):
            r = part['rotation']
            self.assertEqual(r[2], 1)
            self.assertEqual(mod.quarter_turn_orientation(r[0],r[1],r[3] if len(r)>3 else 0)[1], (-y,z,-x))
        rolled = self.compile(compiler, **{'ae2:z':90})['split_models']
        for part, (x,y,z) in zip(rolled, expected):
            r = part['rotation']
            self.assertEqual(mod.quarter_turn_orientation(r[0],r[1],r[3] if len(r)>3 else 0)[1], (y,-x,z))

    def test_owned_prefix_and_separate_source_context(self):
        put(self.root,'assets/fixture/models/pressure_child.json', {'parent':'fixture:pressure_tube',
            'prefix':'invalid_missing', 'textures':{'pipe':'fixture:missing_outer'},
            'visibility':{'all':False}, 'transform':{'translation':[2,0,0]}})
        compiler = self.compiler()
        result = self.compile(compiler, 'fixture:pressure_child')
        self.assertEqual(len(result['split_models']), 6)
        self.assertEqual(set(compiler.textures), {'fixture:pressure_tube'})
        with self.assertRaisesRegex(mod.ImportError, 'missing pressure-tube disconnected model'):
            self.compile(compiler, {'parent':'fixture:pressure_child','loader':'pneumaticcraft:pressure_tube'})
        self.assertTrue(any('outer pressure-tube' in w for w in compiler.warnings))

    def test_explicit_loader_reset_and_empty_prefix_are_distinct(self):
        put(self.root,'assets/pneumaticcraft/models/block/pressure_tube_disconnected.json', self.source)
        compiler = self.compiler()
        result = self.compile(compiler, {'parent':'fixture:pressure_tube','loader':'pneumaticcraft:pressure_tube'})
        self.assertEqual(len(result['split_models']), 6)
        with self.assertRaisesRegex(mod.ImportError, 'block/_pressure_tube_disconnected'):
            self.compile(compiler, {'loader':'pneumaticcraft:pressure_tube','prefix':''})

    def test_source_and_pixel_overrides_take_priority(self):
        override = self.root/'override'
        source = copy.deepcopy(self.source)
        source['elements'] = [source['elements'][0]]
        source['elements'][0]['from'] = [1,9,2]
        put(override,'assets/pneumaticcraft/models/block/test_pressure_tube_disconnected.json',source)
        put(override,'assets/fixture/textures/pressure_tube.png',mod.encode_png(1,1,bytes((11,22,33,128))))
        compiler = self.compiler([self.root,override])
        result = self.compile(compiler)
        self.assertEqual(result['split_models'][0]['elements'][0]['coordinates'][:3],(1,9,2))
        self.assertEqual(mod.decode_png(compiler.textures['fixture:pressure_tube'][0])[2],bytes((11,22,33,128)))

    def test_unsupported_source_overrides_do_not_invent_geometry(self):
        for replacement in ({**self.source,'parent':'fixture:missing'},
                            {**self.source,'loader':'neoforge:composite'},
                            {**self.source,'transform':{'translation':[1,0,0]}},
                            {**self.source,'elements':[]}):
            with self.subTest(replacement=list(replacement)):
                put(self.root,'assets/pneumaticcraft/models/block/test_pressure_tube_disconnected.json',replacement)
                with self.assertRaises(mod.ImportError):
                    self.compile(self.compiler())

    def test_null_side_query_omits_culled_faces_but_keeps_unknown_hint(self):
        source = copy.deepcopy(self.source)
        source['elements'] = [source['elements'][0]]
        source['elements'][0]['faces'] = {'up': {'texture':'#pipe','cullface':'north'}}
        put(self.root,'assets/pneumaticcraft/models/block/test_pressure_tube_disconnected.json',source)
        with self.assertRaisesRegex(mod.ImportError,'no visible unculled geometry'):
            self.compile(self.compiler())
        source['elements'][0]['faces']['up']['cullface'] = 'unknown'
        put(self.root,'assets/pneumaticcraft/models/block/test_pressure_tube_disconnected.json',source)
        result = self.compile(self.compiler())
        self.assertEqual(len(result['split_models'][0]['elements'][0]['faces']),1)

    def test_prefix_validation_and_failed_state_rollback(self):
        for value in (1,[],None,'../outside','../','bad:name','/absolute','a//b'):
            with self.subTest(value=value), self.assertRaises(mod.ImportError):
                self.compile(self.compiler(), {'loader':'pneumaticcraft:pressure_tube','prefix':value})
        compiler = self.compiler()
        with self.assertRaises(mod.ImportError):
            compiler.block('fixture:bad',{'multipart':[{'apply':{'model':'fixture:pressure_tube'}},
                                                     {'apply':{'model':'fixture:missing'}}]})
        self.assertFalse(compiler.textures)
        self.assertEqual(compiler.element_count,0)

    def test_block_flattens_six_parts_and_pack_reuses_geometry(self):
        compiler = self.compiler()
        definition = compiler.assets.json('assets/fixture/blockstates/pressure_tube.json')
        rows = compiler.block('fixture:pressure_tube',definition)
        self.assertEqual(len(rows),3)
        self.assertTrue(all(len(row[3])==6 for row in rows))
        self.assertTrue(all(sum(len(e['faces']) for p in row[3] for e in p['elements'])==12 for row in rows))
        destination = self.root/'pack'
        report = mod.compile_pack([self.root],destination)
        self.assertEqual(report['geometries'],1)
        self.assertEqual(report['textures'],1)
        self.assertEqual(report['selectors'],3)
        self.assertEqual((destination/'manifest.mwpack').read_text().count('usemodel\t'),18)


if __name__ == '__main__':
    unittest.main()
