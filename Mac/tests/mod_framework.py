"""Framework's static components use ordinary geometry without ModelData."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

from mod_assets import mod, put
from mod_framework_fixtures import framework_assets

spec = importlib.util.spec_from_file_location('mod_framework_models', Path(__file__).resolve().parents[2] / 'tools/mod_framework_models.py')
framework = importlib.util.module_from_spec(spec)
spec.loader.exec_module(framework)


class FrameworkModels(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.parent = framework_assets(self.root, mod.encode_png)

    def compile(self, model=None, application=None):
        compiler = mod.Compiler(mod.Assets([self.root]))
        if model is not None:
            application = {'model': framework.adapt_open_model(model, mod.ImportError)}
        return compiler, compiler.compile_model(application or {'model': 'fixture:framework_child'})

    def test_components_inherit_textures_and_keep_rotations(self):
        compiler, result = self.compile()
        self.assertEqual(len(result['elements']), 1)
        self.assertEqual(result['elements'][0]['coordinates'], (2, 3, 4, 13, 11, 14, 'x', 11.31, 4, 5, 6, 0))
        self.assertEqual(len(result['elements'][0]['faces']), 6)
        self.assertEqual(set(compiler.textures), {'fixture:framework_override'})
        _, turned = self.compile(application={'model': 'fixture:framework_child', 'y': 90, 'uvlock': True})
        self.assertEqual(turned['rotation'], (0, 90, 1))
        self.assertEqual(result['elements'], turned['elements'])

    def test_adapter_does_not_mutate_cached_model(self):
        before = copy.deepcopy(self.parent)
        adapted = framework.adapt_open_model(self.parent, mod.ImportError)
        self.assertEqual(self.parent, before)
        self.assertNotIn('loader', adapted)
        self.assertNotIn('components', adapted)
        self.assertIsNot(adapted['elements'][0]['faces'], self.parent['components'][0]['faces'])

    def test_explicit_child_loader_does_not_inherit_components(self):
        put(self.root, 'assets/fixture/models/explicit.json', {
            'loader': framework.LOADER, 'parent': 'fixture:framework_parent'})
        with self.assertRaisesRegex(mod.ImportError, 'components'):
            self.compile(application={'model': 'fixture:explicit'})
        put(self.root, 'assets/fixture/models/explicit.json', {
            'loader': framework.LOADER, 'parent': 'fixture:framework_parent', 'components': []})
        with self.assertRaisesRegex(mod.ImportError, 'components'):
            self.compile(application={'model': 'fixture:explicit'})

    def test_components_on_child_without_loader_do_not_replace_custom_geometry(self):
        child = copy.deepcopy(self.parent['components'][0])
        child['to'] = [7, 8, 9]
        put(self.root, 'assets/fixture/models/no_loader.json', {
            'parent': 'fixture:framework_parent', 'components': [child],
            'textures': {'surface': 'fixture:framework_override'}})
        compiler, result = self.compile(application={'model': 'fixture:no_loader'})
        self.assertEqual(result['elements'][0]['coordinates'][:6], (2, 3, 4, 13, 11, 14))
        self.assertEqual(set(compiler.textures), {'fixture:framework_override'})

    def test_default_uvs_use_zero_parse_endpoints(self):
        element = self.parent['components'][0]
        for face in element['faces'].values():
            face.pop('uv')
        _, result = self.compile(self.parent)
        faces = {f[0]: f[1:5] for f in result['elements'][0]['faces']}
        self.assertEqual(faces, framework._ZERO_ENDPOINT_UV)

    def test_element_extra_metadata_is_discarded_and_face_data_survives(self):
        element = self.parent['components'][0]
        element['neoforge_data'] = {'color': 'FFFF0000'}
        element['faces']['north']['neoforge_data'] = {'color': 'FF00FFFF'}
        adapted = framework.adapt_open_model(self.parent, mod.ImportError)
        self.assertNotIn('neoforge_data', adapted['elements'][0])
        self.assertEqual(adapted['elements'][0]['faces']['north']['neoforge_data'], {'color': 'FF00FFFF'})
        _, result = self.compile(self.parent)
        names = {face[6] for face in result['elements'][0]['faces']}
        self.assertEqual(len(names), 2)

    def test_component_override_replaces_parent_list(self):
        child = copy.deepcopy(self.parent['components'][0])
        child['to'] = [7, 8, 9]
        put(self.root, 'assets/fixture/models/explicit.json', {
            'loader': framework.LOADER, 'parent': 'fixture:framework_parent', 'components': [child]})
        _, result = self.compile(application={'model': 'fixture:explicit'})
        self.assertEqual(result['elements'][0]['coordinates'][:6], (2, 3, 4, 7, 8, 9))

    def test_nonstandard_rescale_fails_without_changing_geometry(self):
        for angle in (0, 2.5, 11.31, 17.103, 'bad', [], None):
            source = copy.deepcopy(self.parent)
            source['components'][0]['rotation'].update(angle=angle, rescale=True)
            with self.subTest(angle=angle), self.assertRaisesRegex(mod.ImportError, 'rescale'):
                self.compile(source)
        for angle in (-45, -22.5, 22.5, 45):
            source = copy.deepcopy(self.parent)
            source['components'][0]['rotation'].update(angle=angle, rescale=True)
            _, result = self.compile(source)
            self.assertEqual(result['elements'][0]['coordinates'][-1], 1)

    def test_bounded_and_malformed_components(self):
        for components in (None, {}, [], [None], self.parent['components'] * 2049):
            with self.subTest(kind=type(components).__name__), self.assertRaises(mod.ImportError):
                self.compile({**self.parent, 'components': components})
        for change in ({'from': [-257, 0, 0]}, {'to': [0, float('inf'), 0]},
                       {'rotation': {'axis': 'x', 'angle': 46}}, {'rotation': []},
                       {'faces': {'sideways': {'texture': '#surface'}}}):
            source = copy.deepcopy(self.parent)
            source['components'][0].update(change)
            with self.subTest(change=change), self.assertRaises(mod.ImportError):
                self.compile(source)

    def test_unhandled_root_transform_or_visibility_fails(self):
        for field, value in (('transform', {'translation': [1, 0, 0]}), ('visibility', {'part': False}), ('transform', [])):
            with self.subTest(field=field), self.assertRaisesRegex(mod.ImportError, 'root transforms'):
                self.compile({**self.parent, field: value})


if __name__ == '__main__':
    unittest.main()
