"""CloudFX float32/RNG goldens and safe atlas-source resolution."""
import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from mod_assets import mod, put, simple, cloudfx_assets

cloud = mod._cloudfx_module


class CloudFxTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='mineways-cloudfx-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        simple(self.source)
        cloudfx_assets(self.source)

    def compiler(self, sources=None):
        assets = mod.Assets(sources or [self.source])
        self.addCleanup(assets.close)
        return mod.Compiler(assets)

    def declaration(self, **extra):
        return {'type': 'xycraft_core:fxsprite', 'id': cloud.CLOUD_IDS[0],
                'width': 2, 'height': 2, 'alpha': 255, 'min_intensity': .0065, **extra}

    def atlas(self, root, sources):
        put(root, 'assets/minecraft/atlases/blocks.json', {'sources': sources})

    def test_rng_and_warmed_frame_match_independent_java_float_reference(self):
        # Captured from a standalone Java float reference transcribed from the
        # shipped bytecode. It imports no mod classes and uses no Minecraft code.
        random = cloud._Random(0)
        self.assertEqual([struct.pack('>f', random.next_float()).hex() for _ in range(8)],
                         ['3de6b998', '3f31934b', '3e163f54', '3f17a431',
                          '3f6a50e6', '3ef3bc3a', '3f6dc5b7', '3f6c9de0'])
        goldens = [(255, .0065, '632cc6f7a43a01604328539f3340cfe1a8a2ba9024a7a8a35199220aef7e1a6f'),
                   (128, .0065, '21970e878f49ffa0c8512b7e1fafccf72cbbcb5daef68e09b2b5757c6ff29696'),
                   (255, .009, '15c1677bdd81a7c275aed22dd2aadec7451257895b49a7c9e2638ca3552ff91d')]
        for alpha, intensity, digest in goldens:
            width, height, pixels = cloud.generate(self.declaration(width=32, height=32, alpha=alpha, min_intensity=intensity))
            self.assertEqual((width, height), (32, 32))
            self.assertEqual(hashlib.sha256(pixels).hexdigest(), digest)
            self.assertEqual(set(pixels[3::4]), {alpha})

    def test_generator_bounds_and_signed_alpha_match_java(self):
        self.assertEqual(cloud.generate(self.declaration(alpha=-1))[2], cloud.generate(self.declaration())[2])
        for extra in ({'width': True}, {'width': 3, 'height': 3}, {'width': 128, 'height': 128},
                      {'width': 2, 'height': 4}, {'alpha': True}, {'alpha': 1 << 31},
                      {'min_intensity': float('nan')}, {'min_intensity': float('inf')},
                      {'min_intensity': True}, {'min_intensity': 10**400}):
            with self.subTest(extra=extra), self.assertRaises(mod.ImportError):
                cloud.generate(self.declaration(**extra), mod.ImportError)

    def test_atlas_stack_combines_sources_and_last_supplier_wins(self):
        later = self.root / 'later'
        self.atlas(later, [{'type': 'single', 'resource': 'absent:irrelevant'}])
        c = self.compiler([self.source, later])
        name = c.texture(cloud.CLOUD_IDS[0])
        self.assertEqual(mod.decode_png(c.textures[name][0])[:2], (32, 32))
        self.assertEqual(len(c.assets.atlas_sources), 2)
        self.atlas(later, [self.declaration(alpha=7)])
        c = self.compiler([self.source, later])
        name = c.texture(cloud.CLOUD_IDS[0])
        self.assertEqual(mod.decode_png(c.textures[name][0]), cloud.generate(self.declaration(alpha=7)))
        archive = self.root / 'later.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            z.write(later / 'assets/minecraft/atlases/blocks.json', 'assets/minecraft/atlases/blocks.json')
        c = self.compiler([self.source, archive])
        self.assertEqual(mod.decode_png(c.textures[c.texture(cloud.CLOUD_IDS[0])][0])[2][3], 7)

    def test_explicit_png_wins_and_bad_png_does_not_generate_fallback(self):
        name = cloud.CLOUD_IDS[0]
        path = 'assets/xycraft_core/textures/block/cloudfx.png'
        pixels = bytes((11, 22, 33, 44)) * 4
        put(self.source, path, mod.encode_png(2, 2, pixels))
        self.atlas(self.source, [self.declaration(width=999999)])
        c = self.compiler()
        self.assertEqual(mod.decode_png(c.textures[c.texture(name)][0]), (2, 2, pixels))
        self.assertFalse(c.cloudfx_frames)
        self.assertFalse(c.warnings)
        put(self.source, path, b'invalid')
        with self.assertRaises(mod.ImportError):
            self.compiler().texture(name)

    def test_filters_readd_and_explicit_sprite_alias_preserve_order(self):
        later = self.root / 'later'
        remove = {'type': 'filter', 'pattern': {'namespace': '^xycraft_core$', 'path': '^block/cloudfx.*$'}}
        self.atlas(later, [remove])
        with self.assertRaisesRegex(mod.ImportError, 'missing CloudFX atlas declaration'):
            self.compiler([self.source, later]).texture(cloud.CLOUD_IDS[0])
        self.atlas(later, [remove, self.declaration(alpha=22)])
        c = self.compiler([self.source, later])
        self.assertEqual(mod.decode_png(c.textures[c.texture(cloud.CLOUD_IDS[0])][0])[2][3], 22)
        self.atlas(later, [{'type': 'single', 'resource': 'sample:block/stone', 'sprite': cloud.CLOUD_IDS[0]}])
        c = self.compiler([self.source, later])
        self.assertEqual(c.textures[c.texture(cloud.CLOUD_IDS[0])], c.textures['sample:block/stone'])
        self.assertFalse(c.cloudfx_frames)
        self.atlas(later, [{'type': 'filter', 'pattern': {'path': '(a+)+$'}}])
        with self.assertRaisesRegex(mod.ImportError, 'unsupported CloudFX atlas filter'):
            self.compiler([self.source, later]).texture(cloud.CLOUD_IDS[0])

    def test_empty_directory_alias_uses_supplied_texture(self):
        later = self.root / 'later'
        pixels = bytes((7, 11, 13, 17)) * 4
        put(later, 'assets/xycraft_core/textures/cloudfx.png', mod.encode_png(2, 2, pixels))
        self.atlas(later, [{'type': 'directory', 'source': '', 'prefix': 'block/'}])
        c = self.compiler([self.source, later])
        self.assertEqual(mod.decode_png(c.textures[c.texture(cloud.CLOUD_IDS[0])][0]), (2, 2, pixels))
        self.assertFalse(c.cloudfx_frames)

    def test_unknown_fx_fields_cannot_impersonate_internal_alias(self):
        self.atlas(self.source, [self.declaration(resource='sample:block/stone')])
        c = self.compiler()
        expected = cloud.generate(self.declaration())
        self.assertEqual(mod.decode_png(c.textures[c.texture(cloud.CLOUD_IDS[0])][0]), expected)
        self.atlas(self.source, [self.declaration(width=3, height=3, resource='sample:block/stone')])
        with self.assertRaisesRegex(mod.ImportError, 'power-of-two'):
            self.compiler().texture(cloud.CLOUD_IDS[0])

    def test_missing_declarations_and_invalid_metadata_do_not_invent_textures(self):
        self.atlas(self.source, [])
        with self.assertRaisesRegex(mod.ImportError, 'missing CloudFX atlas declaration'):
            self.compiler().texture(cloud.CLOUD_IDS[0])
        self.atlas(self.source, [self.declaration(width=3, height=3)])
        with self.assertRaisesRegex(mod.ImportError, 'power-of-two'):
            self.compiler().texture(cloud.CLOUD_IDS[0])
        self.atlas(self.source, [self.declaration()] * 4097)
        with self.assertRaisesRegex(mod.ImportError, 'source count'):
            self.compiler().texture(cloud.CLOUD_IDS[0])

    def test_failed_state_rolls_back_warning_but_reuses_bounded_frame(self):
        c = self.compiler()
        failed = {'variants': {'': {'model': {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16],
            'faces': {'down': {'texture': cloud.CLOUD_IDS[0]}, 'up': {'texture': 'absent:fail'}}}]}}}}
        with self.assertRaises(mod.ImportError):
            c.block('sample:failed', failed)
        self.assertFalse(c.textures)
        self.assertFalse(c.warnings)
        self.assertEqual(len(c.cloudfx_frames), 1)
        c.texture(cloud.CLOUD_IDS[0])
        self.assertTrue(any('fixed seed 0' in w and 'animation is frozen' in w for w in c.warnings))


if __name__ == '__main__':
    unittest.main()
