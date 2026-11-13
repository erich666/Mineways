"""Original barrel and backpack assets shared by native and importer tests."""
import json


def put(root, path, value):
    path = root / path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())


def dynamic_storage_assets(root, encode_png):
    colors = {'wood': (50, 100, 150, 255), 'open': (170, 90, 20, 255),
              'tier': (90, 200, 130, 255), 'cloth': (250, 200, 100, 192),
              'clips': (20, 180, 240, 255), 'unused': (255, 0, 0, 255)}
    for name, color in colors.items():
        put(root, f'assets/fixturestorage/textures/{name}.png', encode_png(2, 2, bytes(color) * 4))
    for name, top, texture in [('closed', 14, 'wood'), ('open', 10, 'open'), ('flat', 16, 'wood'), ('tier', 15, 'tier')]:
        put(root, f'assets/fixturestorage/models/{name}.json', {
            'textures': {'surface': 'fixturestorage:' + texture}, 'elements': [
                {'from': [1, 0, 2], 'to': [15, top, 14], 'faces': {
                    'north': {'texture': '#surface', 'uv': [1, 2, 15, 14]},
                    'up': {'texture': '#surface', 'uv': [2, 1, 14, 15], 'rotation': 90}}}]})
    barrel = {'loader': 'sophisticatedstorage:barrel', 'model_parts': {
        'base': {'model': 'fixturestorage:closed'}, 'base_open': {'model': 'fixturestorage:open'},
        'tier': {'model': 'fixturestorage:tier'}}}
    put(root, 'assets/fixturestorage/models/barrel.json', barrel)
    put(root, 'assets/fixturestorage/models/barrel_flat.json', {
        'parent': 'fixturestorage:barrel', 'loader': 'sophisticatedstorage:barrel',
        'model_parts': {'base': {'model': 'fixturestorage:flat'}}})
    definition = {'variants': {
        'facing=north,flat_top=false': {'model': 'fixturestorage:barrel'},
        'facing=east,flat_top=false': {'model': 'fixturestorage:barrel', 'y': 90},
        'facing=up,flat_top=true': {'model': 'fixturestorage:barrel_flat', 'x': 270}}}
    put(root, 'assets/fixturestorage/blockstates/barrel.json', definition)
    parts = ('base', 'left_pouch', 'right_pouch', 'front_pouch', 'straps', 'left_tank', 'right_tank', 'battery')
    for index, part in enumerate(parts):
        put(root, f'assets/sophisticatedbackpacks/models/block/backpack_{part}.json', {
            'textures': {'cloth': 'fixturestorage:cloth', 'clips': 'fixturestorage:unused'},
            'elements': [{'from': [index, 1, 2], 'to': [index + 1, 13, 14], 'faces': {
                'north': {'texture': '#cloth', 'uv': [1, 2, 15, 14], 'tintindex': 0},
                'south': {'texture': '#cloth', 'uv': [1, 2, 15, 14], 'tintindex': 1},
                'up': {'texture': '#clips', 'uv': [2, 1, 14, 15]}}}]})
    backpack = {'loader': 'sophisticatedbackpacks:backpack',
                'textures': {'clips': '#tier', 'tier': 'fixturestorage:clips', 'cloth': 'fixturestorage:unused'}}
    put(root, 'assets/fixturestorage/models/backpack.json', backpack)
    definition = {'variants': {'facing=north': {'model': 'fixturestorage:backpack'},
                               'facing=east': {'model': 'fixturestorage:backpack', 'y': 90}}}
    put(root, 'assets/fixturestorage/blockstates/backpack.json', definition)
    return barrel, backpack
