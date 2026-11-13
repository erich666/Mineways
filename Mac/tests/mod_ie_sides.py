"""Configurable-side cubes preserve the shipped no-ModelData face mapping."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_ie_sides_fixtures import ie_sides_assets

spec = importlib.util.spec_from_file_location('ie_sides_helper', Path(__file__).resolve().parents[2] / 'tools/mod_ie_sides.py')
sides = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sides)


class ConfigurableSides(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        ie_sides_assets(self.root, mod.encode_png)

    def compile(self, kind='side_top_bottom', base='fixture:block/ie_sides', rotation=(0, 0, 0), extra=None, selector=None, depth=0):
        compiler = mod.Compiler(mod.Assets([self.root]))
        model = {'loader': sides.LOADER, 'type': kind, 'base_name': base, **(extra or {})}
        result = sides.compile_conf_sides(compiler, 'fixture:ie_sides', model, rotation, selector, depth, mod.ImportError)
        return compiler, result

    def test_each_naming_family_selects_actual_default_resources(self):
        expected = {
            'side_top_bottom': ['down_none', 'up_none', 'side_none', 'side_none', 'side_none', 'side_none'],
            'side_vertical': ['up_none', 'up_none', 'side_none', 'side_none', 'side_none', 'side_none'],
            'vertical': ['up_none', 'up_none', 'side', 'side', 'side', 'side'],
            'all_same_texture': ['side_none'] * 6,
        }
        for kind in sides.TYPES:
            with self.subTest(kind=kind):
                compiler, result = self.compile(kind)
                self.assertEqual(result['rotation'], (0, 0, 0))
                element = result['elements'][0]
                self.assertEqual(element['coordinates'][:6], (0, 0, 0, 16, 16, 16))
                by_side = {face[0]: face for face in element['faces']}
                for direction, suffix in zip(sides.DIRECTIONS, expected[kind]):
                    face = by_side[direction]
                    self.assertEqual(face[1:6], (0, 0, 16, 16, 0))
                    self.assertEqual(face[6], 'fixture:block/ie_sides_' + suffix)
                    self.assertEqual(face[7], direction)
                self.assertTrue(any('saved sideConfig' in warning for warning in compiler.warnings))

    def test_source_texture_pixels_are_unchanged(self):
        compiler, _ = self.compile()
        for name, (encoded, _, _) in compiler.textures.items():
            source = self.root / mod.asset_path(name, 'textures', '.png')
            self.assertEqual(mod.decode_png(encoded), mod.decode_png(source.read_bytes()))

    def test_palette_and_supplied_rotations_do_not_turn_absolute_faces(self):
        _, reference = self.compile()
        for rotation in ((90, 270, 1), (0, 0, 0, 90), (180, 90, 0)):
            compiler, result = self.compile(rotation=rotation, selector={'facing': 'east', 'sideConfig': 'out'})
            self.assertEqual(result, reference)
            self.assertTrue(any('ignored by the shipped' in warning for warning in compiler.warnings))
        _, transformed = self.compile(extra={'transform': {'translation': [2, 3, 4], 'rotation': {'z': 90}}})
        self.assertEqual(transformed, reference)

    def test_outer_textures_and_elements_do_not_replace_generated_faces(self):
        _, expected = self.compile()
        _, actual = self.compile(extra={'textures': {'all': 'fixture:absent'}, 'elements': [], 'visibility': {'all': False}})
        self.assertEqual(actual, expected)

    def test_unknown_type_invalid_base_and_missing_textures_fail(self):
        for kind in ('unknown', None, [], {}, 2):
            with self.subTest(kind=kind), self.assertRaises(mod.ImportError):
                self.compile(kind=kind)
        for base in (None, '', [], '../outside', 'fixture:../outside', 'fixture:missing'):
            with self.subTest(base=base), self.assertRaises(mod.ImportError):
                self.compile(base=base)
        with self.assertRaisesRegex(mod.ImportError, 'depth'):
            self.compile(depth=63)

    def test_loader_dispatch_and_canonical_selectors(self):
        compiler = mod.Compiler(mod.Assets([self.root]))
        for kind in sides.TYPES:
            name = 'fixture:ie_sides_' + kind
            rows = compiler.block(name, compiler.assets.json(mod.asset_path(name, 'blockstates')))
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0][3], rows[1][3])
            self.assertEqual({row[0] for row in rows}, {name + '[orientation=plain]', name + '[orientation=turned]'})
        self.assertFalse(compiler.unsupported_states)

    def test_custom_geometry_fields_inherit_only_without_a_new_loader(self):
        put(self.root, 'assets/fixture/models/child.json', {
            'parent': 'fixture:ie_sides_side_top_bottom', 'base_name': 'fixture:missing', 'type': 'unknown'})
        compiler = mod.Compiler(mod.Assets([self.root]))
        child = compiler.compile_model({'model': 'fixture:child'})
        self.assertEqual(child, compiler.compile_model({'model': 'fixture:ie_sides_side_top_bottom'}))
        for fields in ({}, {'type': 'vertical'}, {'base_name': 'fixture:block/ie_sides'}):
            put(self.root, 'assets/fixture/models/explicit.json', {
                'loader': sides.LOADER, 'parent': 'fixture:ie_sides_side_top_bottom', **fields})
            compiler = mod.Compiler(mod.Assets([self.root]))
            with self.assertRaises(mod.ImportError):
                compiler.compile_model({'model': 'fixture:explicit'})
        put(self.root, 'assets/fixture/models/explicit.json', {
            'loader': sides.LOADER, 'parent': 'fixture:ie_sides_side_top_bottom',
            'base_name': 'fixture:block/ie_sides', 'type': 'vertical'})
        compiler = mod.Compiler(mod.Assets([self.root]))
        result = compiler.compile_model({'model': 'fixture:explicit'})
        _, expected = self.compile(kind='vertical')
        self.assertEqual(result, expected)


if __name__ == '__main__':
    unittest.main()
