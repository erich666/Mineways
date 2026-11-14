"""Exercise launcher discovery, source precedence and cache reuse with small local packs."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from unittest.mock import patch

import prepare_modpack as prep


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Mineways test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.package = self.root / 'Mineways preview'
        self.package.mkdir()
        self.instance = self.root / 'instances/Prism pack'
        self.game = self.instance / 'minecraft'
        self.world = self.game / 'saves/World One'
        self.world.mkdir(parents=True)
        (self.world / 'level.dat').write_bytes(b'fixture')
        (self.game / 'mods').mkdir()
        (self.instance / 'mmc-pack.json').write_text(json.dumps({'components': [
            {'uid': 'net.minecraft', 'version': '1.21.1'}, {'uid': 'net.fabricmc.fabric-loader', 'version': '0.17.2'}]}))
        self.client = self.root / 'libraries/com/mojang/minecraft/1.21.1/minecraft-1.21.1-client.jar'
        self.jar(self.client, {'assets/minecraft/blockstates/stone.json': '{}'})
        self.mod = self.game / 'mods/example.jar'

    def jar(self, path, files):
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, 'w') as jar:
            for name, contents in files.items():
                jar.writestr(name, contents)

    def test_client_only_and_unrenderable_pack(self):
        self.assertEqual(prep.prepare(self.package, self.game, self.world / 'level.dat'), 'VANILLA')
        self.jar(self.mod, {'assets/demo/blockstates/broken.json': '{}'})
        with self.assertRaisesRegex(ValueError, 'no supported mod'):
            prep.prepare(self.package, self.game, self.world)

    def test_enabled_resource_order_cache_and_world(self):
        packs = self.game / 'resourcepacks'
        data = 'data/example/dimension/demo.json'
        def dimension(height):
            return json.dumps({'type': {'min_y': 0, 'height': height, 'coordinate_scale': 1}})
        self.jar(self.mod, {data: dimension(64)})
        self.jar(self.game / 'mods/unused.jar.disabled', {data: 'bad'})
        for filename, height in [('one.zip', 128), ('two.zip', 192)]:
            self.jar(packs / filename, {data: dimension(height)})
        options = self.game / 'options.txt'
        options.write_text('resourcePacks:["vanilla","file/two.zip","file/one.zip"]')
        self.assertEqual(prep.launcher_sources(self.instance, self.world),
                         [self.client, self.mod, packs / 'two.zip', packs / 'one.zip'])
        first = Path(prep.prepare(self.package, self.game, self.world))
        self.assertIn('example:demo\t0\t128', first.read_text())
        with patch.object(prep.importer, 'compile_pack', side_effect=AssertionError('cache miss')):
            self.assertEqual(prep.prepare(self.package, self.instance, self.world), str(first))
        options.write_text('resourcePacks:["file/one.zip","file/two.zip"]')
        second = Path(prep.prepare(self.package, self.game, self.world))
        self.assertNotEqual(first, second)
        self.assertIn('example:demo\t0\t192', second.read_text())
        self.jar(self.world / 'resources.zip', {data: dimension(256)})
        third = Path(prep.prepare(self.package, self.game, self.world))
        self.assertIn('example:demo\t0\t256', third.read_text())
        third.write_text('incomplete')
        with self.assertRaisesRegex(ValueError, 'prepared pack files changed'):
            prep.prepare(self.package, self.game, self.world)

    def test_curseforge_loader_and_missing_client(self):
        (self.game / 'minecraftinstance.json').write_text(json.dumps({'gameVersion':'1.21.1',
            'baseModLoader': {'name':'forge-52.0.1'}}))
        # Move the game from Prism's directory to a CurseForge instance.
        game = self.root / 'Instances/My other pack'
        game.parent.mkdir(exist_ok=True)
        self.game.rename(game)
        world = game / 'saves/World One'
        forge = self.root / 'Install/libraries/net/minecraftforge/forge/1.21.1-52.0.1/forge-1.21.1-52.0.1-universal.jar'
        with self.assertRaisesRegex(ValueError, 'forge 52.0.1 assets are missing'):
            prep.launcher_sources(game, world)
        self.jar(forge, {})
        self.assertEqual(prep.launcher_sources(game, world), [self.client, forge])
        self.client.unlink()
        with patch.dict('os.environ', {'APPDATA': str(self.root / 'empty')}), self.assertRaisesRegex(ValueError, '1.21.1 client'):
            prep.launcher_sources(game, world)

    def test_unrelated_world_and_missing_world_data(self):
        with self.assertRaisesRegex(ValueError, 'inside this instance'):
            prep.launcher_sources(self.instance, self.package)
        (self.world / 'level.dat').unlink()
        with self.assertRaisesRegex(ValueError, 'level.dat'):
            prep.launcher_sources(self.instance, self.world)


if __name__ == '__main__':
    unittest.main()
