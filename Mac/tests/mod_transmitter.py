"""Mekanism's absent-ModelData fallback uses exact NONE groups and ordinary OBJ materials."""
import copy
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from mod_assets import mod, put
from mod_transmitter_fixtures import transmitter_assets

SPEC = importlib.util.spec_from_file_location('mod_mekanism_models', Path(__file__).resolve().parents[2] / 'tools/mod_mekanism_models.py')
mekanism = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mekanism)
API = SimpleNamespace(ImportError=mod.ImportError, resource=mod.resource, decode_png=mod.decode_png,
                      encode_png=mod.encode_png, MAX_COMPILED_ELEMENTS=mod.MAX_COMPILED_ELEMENTS,
                      number=mod.number, expanded_uv=mod.expanded_uv, REPEATED_UV_WARNING=mod.REPEATED_UV_WARNING)


class TransmitterTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-transmitter-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.model, self.definition = transmitter_assets(self.root, mod.encode_png)

    def compile(self, model=None, rotation=(0, 0, 0), sources=None):
        assets = mod.Assets(sources or [self.root])
        self.addCleanup(assets.close)
        compiler = mod.Compiler(assets)
        result = mekanism.compile_transmitter(compiler, self.model if model is None else model,
                                              rotation, API, mod._obj_module)
        return compiler, result

    def test_only_six_exact_disconnected_groups_survive(self):
        compiler, result = self.compile()
        self.assertEqual(len(result['polygons']), 6)
        positions = [v[:3] for face in result['polygons'] for v in face['vertices']]
        self.assertEqual([min(p[i] for p in positions) for i in range(3)], [4, 4, 4])
        self.assertEqual([max(p[i] for p in positions) for i in range(3)], [12, 12, 12])
        self.assertTrue(all(len(face['vertices']) == 4 for face in result['polygons']))
        self.assertFalse(any('missing' in key or 'glass' in key for key in compiler.textures))
        self.assertTrue(any('connections, contents, colors and optional glass' in w for w in compiler.warnings))

    def test_default_uses_neoforge_flip_v_and_material_color(self):
        compiler, result = self.compile()
        self.assertEqual([vertex[3:] for vertex in result['polygons'][0]['vertices']],
                         [(0, 12), (16, 12), (16, 4), (0, 4)])
        texture = result['polygons'][0]['texture']
        width, height, pixels = mod.decode_png(compiler.textures[texture][0])
        self.assertEqual((width, height, pixels), (2, 2, bytes((90, 120, 15, 255)) * 4))
        no_flip = copy.deepcopy(self.model)
        no_flip.pop('flip_v')
        _, result = self.compile(no_flip)
        self.assertEqual([vertex[3:] for vertex in result['polygons'][0]['vertices']],
                         [(0, 4), (16, 4), (16, 12), (0, 12)])

    def test_visibility_override_does_not_enable_unknown_or_nested_groups(self):
        path = self.root / 'assets/fixture/models/transmitter.obj.mek'
        path.write_text(path.read_text() + 'o downNONE\ng nested\nusemtl hidden\nf 1/1 2/2 3/3\n')
        _, result = self.compile()
        self.assertEqual(len(result['polygons']), 6)
        # An ordinary OBJ still respects the JSON visibility map and resolves its visible hidden material.
        with self.assertRaisesRegex(mod.ImportError, 'missing texture'):
            assets = mod.Assets([self.root])
            self.addCleanup(assets.close)
            mod._obj_module.compile_obj(mod.Compiler(assets), self.model, (0,0,0), API)

    def test_rotation_transform_and_resource_override_remain_effective(self):
        override = self.root / 'overrides'
        put(override, 'assets/fixture/textures/transmitter.png', mod.encode_png(2, 2, bytes((80,40,200,255))*4))
        model = {**self.model, 'transform': {'translation': [0.25,0,0]}}
        compiler, result = self.compile(model, (90,270,1), [self.root, override])
        self.assertEqual(result['rotation'], (90,270,1))
        self.assertEqual(min(v[0] for p in result['polygons'] for v in p['vertices']), 8)
        pixels = mod.decode_png(compiler.textures[result['polygons'][0]['texture']][0])[2]
        self.assertEqual(pixels, bytes((40,40,50,255))*4)

    def test_parent_texture_aliases_and_overridden_obj_geometry_are_preserved(self):
        parent = copy.deepcopy(self.model)
        parent['textures'] = {'core': '#surface', 'surface': 'fixture:transmitter'}
        put(self.root, 'assets/fixture/models/transmitter_parent.json', parent)
        put(self.root, 'assets/fixture/models/transmitter_child.json', {'parent': 'fixture:transmitter_parent'})
        override = self.root / 'overrides'
        source = (self.root / 'assets/fixture/models/transmitter.obj.mek').read_text()
        lines = []
        for line in source.splitlines():
            if line.startswith('v '):
                fields = line.split()
                line = 'v ' + str(float(fields[1]) + 0.25) + ' ' + ' '.join(fields[2:])
            lines.append(line)
        put(override, 'assets/fixture/models/transmitter.obj.mek', ('\n'.join(lines) + '\n').encode())
        assets = mod.Assets([self.root, override])
        self.addCleanup(assets.close)
        compiler = mod.Compiler(assets)
        result = compiler.compile_model({'model': 'fixture:transmitter_child'})
        self.assertEqual(len(result['polygons']), 6)
        self.assertEqual(min(v[0] for p in result['polygons'] for v in p['vertices']), 8)
        self.assertEqual(max(v[0] for p in result['polygons'] for v in p['vertices']), 16)
        self.assertFalse(any('unresolved' in w for w in compiler.warnings))

    def test_malformed_inputs_keep_existing_validation(self):
        for change in ({'glass': []}, {'flip_v': 1}, {'model': 'fixture:models/../escape.obj'},
                       {'visibility': {'downNONE': 1}}, {'transform': {'scale': 'invalid'}}):
            with self.subTest(change=change), self.assertRaises(mod.ImportError):
                self.compile({**self.model, **change})
        for groups in (['downNONE'], {1}, {'x'*1024}):
            assets = mod.Assets([self.root])
            self.addCleanup(assets.close)
            with self.assertRaisesRegex(mod.ImportError, 'trusted OBJ visibility'):
                mod._obj_module.compile_obj(mod.Compiler(assets), self.model, (0,0,0), API, visible_groups=groups)

    def test_dispatched_blockstates_keep_geometry_and_rotations(self):
        assets = mod.Assets([self.root])
        self.addCleanup(assets.close)
        compiler = mod.Compiler(assets)
        rows = compiler.block('fixture:transmitter', self.definition)
        self.assertEqual(len(rows), 2)
        self.assertFalse(compiler.unsupported_states)
        for state, _, _, models in rows:
            self.assertEqual(len(models[0]['polygons']), 6)
            self.assertEqual(models[0]['rotation'], (0,90,0) if 'east' in state else (0,0,0))


if __name__ == '__main__':
    unittest.main()
