"""Numeric multipart selectors retain Minecraft's textual property identity."""
import copy
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put, simple


class MultipartNumberTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mineways-multipart-')
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        simple(self.source)
        self.assets = mod.Assets([self.source])
        self.addCleanup(self.assets.close)

    def compile(self, definition):
        compiler = mod.Compiler(self.assets)
        return compiler, compiler.block('sample:multipart', copy.deepcopy(definition))

    @staticmethod
    def application(width):
        return {'model': {'elements': [{'from': [0, 0, 0], 'to': [width, 16, 16],
                'faces': {'north': {'texture': 'sample:block/stone'}}}]}}

    def test_integer_values_equal_string_values_without_rounding(self):
        values = (0, 2, 10, 2147483647, 10 ** 30)
        numeric = {'multipart': [{'when': {'level': value}, 'apply': self.application(i + 1)}
                                 for i, value in enumerate(values)]}
        textual = copy.deepcopy(numeric)
        for part in textual['multipart']:
            part['when']['level'] = str(part['when']['level'])
        _, actual = self.compile(numeric)
        _, expected = self.compile(textual)
        self.assertEqual(actual, expected)
        self.assertEqual({row[0] for row in actual}, {'sample:multipart[level=' + str(v) + ']' for v in values})
        self.assertEqual(len(actual), len(values))

    def test_nested_numeric_and_boolean_conditions_select_exact_geometry(self):
        definition = {'multipart': [
            {'apply': self.application(1)},
            {'when': {'OR': [{'level': 0}, {'AND': [{'level': 1}, {'powered': True}]}]},
             'apply': self.application(2)},
            {'when': {'level': '0|1', 'powered': False}, 'apply': self.application(3)},
        ]}
        compiler, rows = self.compile(definition)
        widths = {name: [model['elements'][0]['coordinates'][3] for model in models]
                  for name, _, _, models in rows}
        self.assertEqual(widths, {
            'sample:multipart': [1],
            'sample:multipart[level=0,powered=false]': [1, 2, 3],
            'sample:multipart[level=0,powered=true]': [1, 2],
            'sample:multipart[level=1,powered=false]': [1, 3],
            'sample:multipart[level=1,powered=true]': [1, 2],
        })
        self.assertFalse(compiler.unsupported_states)
        self.assertFalse(compiler.warnings)

    def test_malformed_numeric_and_nonprimitive_values_are_rejected(self):
        bad_values = (-1, 1.0, 1.5, -0.0, float('nan'), float('inf'), -float('inf'),
                      None, [], [1], {}, {'value': 1}, 10 ** 5000)
        for value in bad_values:
            with self.subTest(type=type(value).__name__):
                definition = {'multipart': [{'when': {'level': value}, 'apply': self.application(1)}]}
                with self.assertRaisesRegex(mod.ImportError, 'multipart property|state selector'):
                    self.compile(definition)
        for key in (1, None, ('level',)):
            with self.subTest(key=key):
                with self.assertRaisesRegex(mod.ImportError, 'multipart property'):
                    self.compile({'multipart': [{'when': {key: 1}, 'apply': self.application(1)}]})

    def test_numeric_conditions_keep_expansion_and_selector_limits(self):
        definition = {'multipart': [
            {'when': {'level_' + str(i): value for i in range(13)}, 'apply': self.application(1)}
            for value in (0, 1)]}
        compiler = mod.Compiler(self.assets)
        with self.assertRaisesRegex(mod.ImportError, '4096 selectors'):
            compiler.block('sample:multipart', definition)
        self.assertEqual(compiler.element_count, 0)
        self.assertFalse(compiler.textures)
        with self.assertRaisesRegex(mod.ImportError, 'state selector exceeds runtime limit'):
            self.compile({'multipart': [{'when': {'level': 10 ** 1023}, 'apply': self.application(1)}]})

    def test_file_json_numeric_syntax_is_not_coerced_to_other_values(self):
        for literal in ('1.0', '1e0', '1e400', 'NaN', 'Infinity', 'null', '[1]', '{"value":1}'):
            data = ('{"multipart":[{"when":{"level":' + literal + '},"apply":{"model":"sample:block/stone"}}]}').encode()
            put(self.source, 'assets/sample/blockstates/input.json', data)
            assets = mod.Assets([self.source])
            try:
                with self.assertRaises(mod.ImportError):
                    definition = assets.json('assets/sample/blockstates/input.json')
                    mod.Compiler(assets).block('sample:input', definition)
            finally:
                assets.close()


if __name__ == '__main__':
    unittest.main()
