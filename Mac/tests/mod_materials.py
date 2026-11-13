"""Domum's default material path preserves supplied parent geometry and identity."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_material_fixtures import material_assets

spec = importlib.util.spec_from_file_location('mod_material_models', Path(__file__).resolve().parents[2] / 'tools/mod_material_models.py')
material = importlib.util.module_from_spec(spec)
spec.loader.exec_module(material)


class MaterialModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent = material_assets(self.root, mod.encode_png)

    def compile(self, parent='fixture:material_parent', rotation=(0, 0, 0), paths=None, depth=0):
        compiler = mod.Compiler(mod.Assets(paths or [self.root]))
        wrapper = {'loader': material.DOMUM_LOADER, 'parent': parent,
                   'textures': {'surface': 'fixture:material_outer'}}
        result = material.compile_domum(compiler, 'fixture:material_wrapper', wrapper, rotation,
                                       {}, depth, mod.ImportError, 'fixture:material_block')
        return compiler, result

    def test_exact_separate_parent_without_wrapper_texture_leakage(self):
        compiler, result = self.compile()
        self.assertEqual(result, compiler.compile_model({'model': 'fixture:material_parent'}))
        self.assertEqual(result['elements'][0]['coordinates'][:6], (1, 2, 3, 12, 14, 15))
        self.assertEqual(result['elements'][0]['faces'][0][:6], ('north', 2, 3, 13, 15, 90))
        self.assertEqual(set(compiler.textures), {'fixture:material_default'})
        self.assertTrue(any('saved block-entity material replacements' in w for w in compiler.warnings))

    def test_rotations_uvlock_and_parent_element_rotation(self):
        self.parent['elements'][0]['rotation'] = {'axis': 'y', 'angle': -22.5, 'origin': [4, 5, 6], 'rescale': True}
        put(self.root, 'assets/fixture/models/material_parent.json', self.parent)
        for rotation in ((90, 270, 1), (0, 0, 0, 90)):
            _, result = self.compile(rotation=rotation)
            self.assertEqual(result['rotation'], rotation)
            self.assertEqual(result['elements'][0]['coordinates'][6:], ('y', -22.5, 4, 5, 6, 1))

    def test_only_explicit_empty_leaf_is_invisible(self):
        compiler, result = self.compile('fixture:material_empty_parent')
        self.assertTrue(result['invisible'])
        self.assertEqual(result['elements'], [])
        self.assertFalse(compiler.textures)
        for name, value in [('omitted', {'textures': {}}),
                            ('inherited', {'parent': 'fixture:material_empty_parent'}),
                            ('other_loader', {'loader': 'fixture:unknown', 'elements': []})]:
            put(self.root, f'assets/fixture/models/{name}.json', value)
            with self.subTest(name=name), self.assertRaises(mod.ImportError):
                self.compile('fixture:' + name)
        with self.assertRaises(mod.ImportError):
            compiler.compile_model({'model': 'fixture:material_empty_parent'})

    def test_resource_override_replaces_empty_parent_with_real_geometry(self):
        override = self.root / 'override'
        put(override, 'assets/fixture/models/material_empty_parent.json', self.parent)
        _, result = self.compile('fixture:material_empty_parent', paths=[self.root, override])
        self.assertNotIn('invisible', result)
        self.assertEqual(len(result['elements']), 1)

    def test_root_transform_overrides_fail_instead_of_silently_disappearing(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        wrapper = {'parent': 'fixture:material_parent', 'transform': {'translation': [1, 0, 0]}}
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            material.compile_domum(compiler, 'fixture:material_wrapper', wrapper, (0, 0, 0),
                                  {}, 0, mod.ImportError)
        self.parent['transform'] = {'rotation': [0, 45, 0]}
        put(self.root, 'assets/fixture/models/material_parent.json', self.parent)
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compile()
        put(self.root, 'assets/fixture/models/inherited_transform.json', {'parent': 'fixture:material_parent'})
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compile('fixture:inherited_transform')

    def test_missing_invalid_and_cyclic_parents_fail(self):
        for parent in (None, {}, [], '../outside', 'fixture:absent'):
            with self.subTest(parent=parent), self.assertRaises(mod.ImportError):
                self.compile(parent)
        put(self.root, 'assets/fixture/models/cycle.json', {'parent': 'fixture:cycle'})
        with self.assertRaisesRegex(mod.ImportError, 'cyclic'):
            self.compile('fixture:cycle')
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compile('fixture:material_empty_parent', depth=63)

    def test_inherited_geometry_keeps_captured_parent_separate_from_model_parent(self):
        wrapper = {'loader': material.DOMUM_LOADER, 'parent': 'fixture:material_parent'}
        put(self.root, 'assets/fixture/models/domum_wrapper.json', wrapper)
        put(self.root, 'assets/fixture/models/domum_child.json', {
            'parent': 'fixture:domum_wrapper', '_domum_geometry_parent': 'fixture:missing',
            'textures': {'surface': 'fixture:material_outer'}, 'elements': []})
        compiler = mod.Compiler(mod.Assets([self.root]))
        resolved = compiler.model('fixture:domum_child')
        self.assertEqual(resolved['parent'], 'fixture:domum_wrapper')
        self.assertEqual(resolved['_domum_geometry_parent'], 'fixture:material_parent')
        result = compiler.compile_model({'model': 'fixture:domum_child', 'y': 90})
        expected = compiler.compile_model({'model': 'fixture:material_parent', 'y': 90})
        self.assertEqual(result, expected)
        # A new explicit loader captures its own parent, not an inherited marker.
        explicit = compiler.model({'parent': 'fixture:domum_wrapper', 'loader': material.DOMUM_LOADER,
                                   '_domum_geometry_parent': 'fixture:missing'})
        self.assertEqual(explicit['_domum_geometry_parent'], 'fixture:domum_wrapper')

    def test_block_dispatch_preserves_visible_and_empty_states(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        rows = compiler.block('fixture:material_block', compiler.assets.json('assets/fixture/blockstates/material_block.json'))
        self.assertFalse(compiler.unsupported_states)
        self.assertEqual(len(rows), 3)
        by_name = {row[0]: row for row in rows}
        self.assertEqual(by_name['fixture:material_block[facing=up]'][3], [])
        self.assertEqual(by_name['fixture:material_block[facing=up]'][2], 0)
        self.assertEqual(by_name['fixture:material_block[facing=east]'][3][0]['rotation'], (90, 270, 1))

    def test_multipart_empty_component_preserves_visible_geometry(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        rows = compiler.block('fixture:material_multipart', compiler.assets.json('assets/fixture/blockstates/material_multipart.json'))
        self.assertFalse(compiler.unsupported_states)
        self.assertEqual(len(rows), 3)
        for name, _, _, models in rows:
            elements = sum(len(model['elements']) for model in models)
            self.assertEqual(elements, 2 if 'connected=true' in name else 1)
        self.assertEqual(set(compiler.textures), {'fixture:material_default'})
        output = self.root / 'compiled'
        report = mod.compile_pack([self.root], output)
        self.assertFalse(report['unsupported'])
        self.assertFalse(report['unsupported_states'])
        manifest = (output / 'manifest.mwpack').read_text()
        geometries = {}
        for chunk in manifest.split('geometry\t')[1:]:
            if not chunk[0].isdigit():
                continue
            index, body = chunk.split('\n', 1)
            body = body.split('endgeometry\n', 1)[0]
            self.assertTrue('element\t' in body or 'polygon\t' in body)
            geometries[index] = body
        for selector in ('connected=false', 'connected=true'):
            state = manifest.split('state\tfixture:material_multipart[' + selector + ']\t', 1)[1].split('endstate', 1)[0]
            self.assertNotIn('invisible', state)
            references = [line.split('\t')[1] for line in state.splitlines() if line.startswith('usemodel\t')]
            self.assertEqual(len(references), 1 if selector == 'connected=false' else 2)
            self.assertTrue(all(index in geometries for index in references))
        # A wholly empty state retains its explicit identity instead of a bare geometry record.
        state = manifest.split('state\tfixture:material_block[facing=up]\t', 1)[1].split('endstate', 1)[0]
        self.assertIn('invisible\n', state)
        self.assertNotIn('usemodel', state)


if __name__ == '__main__':
    unittest.main()
