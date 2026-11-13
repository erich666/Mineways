"""Missing-resource reports distinguish absent namespaces without hiding failures."""
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from mod_assets import mod, put


class MissingResourceTests(unittest.TestCase):
    def test_report_preserves_failures_and_counts_resource_evidence(self):
        rows = [
            {'block': 'sample:one', 'reason': 'missing texture: absent:block/one'},
            {'block': 'sample:two', 'reason': 'missing model: supplied:block/two'},
            {'block': 'sample:three', 'reason': 'custom model loader: sample:loader'},
        ]
        report = {'unsupported': rows, 'unsupported_states': [
            {'state': 'sample:state[lit=true]', 'reason': 'missing model: data_only:block/model'}]}
        assets = SimpleNamespace(files={'assets/supplied/models/unrelated.json': None,
                                       'data/data_only/dimension/world.json': None})
        original = [(r['block'], r['reason']) for r in rows]
        mod.missing_resource_diagnostics(report, assets)
        self.assertEqual([(r['block'], r['reason']) for r in rows], original)
        self.assertEqual(rows[0]['missing_resource'],
                         {'kind': 'texture', 'name': 'absent:block/one', 'indexed_namespace_present': False})
        self.assertTrue(rows[1]['missing_resource']['indexed_namespace_present'])
        self.assertNotIn('missing_resource', rows[2])
        self.assertEqual(report['missing_resources'], {
            'unsupported': {'namespace_unindexed': 1, 'namespace_indexed': 1},
            'unsupported_states': {'namespace_unindexed': 1, 'namespace_indexed': 0}})

    def test_compiled_report_includes_missing_resource_details(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            put(source, 'assets/sample/blockstates/missing.json', {'variants': {'': {
                'model': 'absent:block/model'}}})
            put(source, 'assets/sample/blockstates/air.json', {'variants': {'': {'model': 'ae2:block/matrix_frame'}}})
            put(source, 'assets/ae2/models/block/matrix_frame.json', {'textures': {}})
            report = mod.compile_pack([source], root / 'pack')
            self.assertEqual(report['blocks'], 1)
            self.assertEqual(len(report['unsupported']), 1)
            self.assertEqual(report['unsupported'][0]['missing_resource']['name'], 'absent:block/model')
            self.assertEqual(report['missing_resources']['unsupported']['namespace_unindexed'], 1)
