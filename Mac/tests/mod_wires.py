"""MoreRed static companions remain visible without accepting wholly dynamic states."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_wire_fixtures import wire_assets


class WireModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mineways-wire-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.definition = wire_assets(self.root, mod.encode_png)

    def compiler(self, paths=None):
        assets = mod.Assets(paths or [self.root])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def test_static_nodes_and_elbows_survive_dynamic_child_omission(self):
        compiler = self.compiler()
        rows = compiler.block('fixture:wire', self.definition)
        self.assertEqual(len(rows), 3)
        counts = {name: sum(len(element['faces']) for model in models for element in model['elements'])
                  for name, _, _, models in rows}
        self.assertEqual(counts, {'fixture:wire[down=false,west=true]': 6,
                                 'fixture:wire[down=true,west=false]': 6,
                                 'fixture:wire[down=true,west=true]': 18})
        self.assertEqual(len(compiler.unsupported_states), 2)  # Generic and all-false state.
        self.assertTrue(all('requires live renderer data' in row['reason'] for row in compiler.unsupported_states))
        self.assertEqual(set(compiler.textures), {'fixture:wire'})
        self.assertTrue(any('neighbor-dependent' in message for message in compiler.warnings))
        self.assertTrue(any('power tint' in message for message in compiler.warnings))
        west = next(models for name, _, _, models in rows if name.endswith('down=false,west=true]'))
        self.assertEqual(west[1]['rotation'], (90,90,0))

    def test_entirely_dynamic_direct_and_nested_models_remain_unsupported(self):
        for model in ['fixture:wire_parts', {'loader':'neoforge:composite', 'children': {
                'dynamic': {'parent':'fixture:wire_parts'}}}]:
            compiler = self.compiler()
            with self.subTest(model=model), self.assertRaisesRegex(mod.ImportError, 'requires live renderer data'):
                compiler.block('fixture:dynamic', {'variants': {'': {'model':model}}})

    def test_mixed_composite_preserves_visible_companion(self):
        compiler = self.compiler()
        model = {'loader':'neoforge:composite', 'children': {
            'dynamic': {'parent':'fixture:wire_parts'}, 'node': {'parent':'fixture:wire_node'}}}
        rows = compiler.block('fixture:mixed', {'variants': {'': {'model':model}}})
        self.assertEqual(len(rows[0][3][0]['elements']), 1)
        self.assertFalse(rows[0][3][0].get('invisible'))

    def test_tint_loader_preserves_standard_geometry_and_parent_overrides(self):
        override = self.root / 'override'
        put(override, 'assets/fixture/models/wire_node.json', {'loader':'morered:rotate_tints',
            'elements':[{'from':[1,2,3], 'to':[4,5,6], 'faces':{'up':{'texture':'fixture:wire','uv':[1,2,3,4]}}}]})
        put(self.root, 'assets/fixture/models/tinted_child.json', {'parent':'fixture:wire_node'})
        compiler = self.compiler([self.root, override])
        model = compiler.compile_model({'model':'fixture:tinted_child','y':90,'uvlock':True})
        self.assertEqual(model['rotation'], (0,90,1))
        self.assertEqual(model['elements'][0]['coordinates'][:6], (1,2,3,4,5,6))
        self.assertEqual(model['elements'][0]['faces'][0][1:5], (1,2,3,4))

    def test_bad_wire_fields_and_tint_transforms_reject(self):
        for value in (None, [], 'fixture:line'):
            with self.subTest(value=value), self.assertRaisesRegex(mod.ImportError, 'inline line and edge'):
                self.compiler().compile_model({'model':{'loader':'morered:wire_parts','line':value,'edge':{}}})
        with self.assertRaisesRegex(mod.ImportError, 'root transforms'):
            self.compiler().compile_model({'model':{'parent':'fixture:wire_node','transform':{'translation':[1,0,0]}}})

    def test_exact_empty_rack_states_are_invisible(self):
        compiler = self.compiler()
        definition = compiler.assets.json('assets/minecolonies/blockstates/blockminecoloniesrack.json')
        rows = compiler.block('minecolonies:blockminecoloniesrack', definition)
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row[2] == 0 and row[3] == [] for row in rows))
        self.assertFalse(compiler.textures)
        for name, selector in [('fixture:rack','variant=blockrackair'),
                               ('minecolonies:blockminecoloniesrack','variant=other')]:
            with self.subTest(name=name,selector=selector), self.assertRaises(mod.ImportError):
                compiler.block(name, {'variants':{selector:{'model':'minecolonies:block/blockairrack'}}})

    def test_nonempty_rack_resource_override_remains_visible(self):
        put(self.root, 'assets/minecolonies/models/block/blockairrack.json', {
            'elements':[{'from':[0,0,0],'to':[16,16,16],'faces':{'north':{'texture':'fixture:wire'}}}]})
        compiler = self.compiler()
        definition = compiler.assets.json('assets/minecolonies/blockstates/blockminecoloniesrack.json')
        rows = compiler.block('minecolonies:blockminecoloniesrack', definition)
        self.assertTrue(all(row[2] == 255 and len(row[3][0]['elements']) == 1 for row in rows))

    def test_pack_serialization_skips_only_dynamic_child_of_visible_states(self):
        output = self.root.parent / (self.root.name + '-pack')
        self.addCleanup(lambda: __import__('shutil').rmtree(output, ignore_errors=True))
        mod.compile_pack([self.root], output)
        text = (output / 'manifest.mwpack').read_text()
        self.assertNotIn('state\tfixture:wire[down=false,west=false]', text)
        self.assertNotIn('state\tfixture:wire\t', text)
        self.assertIn('state\tfixture:wire[down=true,west=false]', text)
        self.assertIn('invisible\n', text)  # Exact MineColonies empty states only.
        for geometry in text.split('geometry\t')[1:]:
            self.assertTrue('element\t' in geometry.split('endgeometry')[0])


if __name__ == '__main__':
    unittest.main()
