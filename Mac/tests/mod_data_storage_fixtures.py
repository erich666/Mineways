"""Original asymmetric assets for data-selected models and empty portable grids."""
import json


def data_storage_assets(root, encode_png):
    def put(path, value):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    colors = {'offline': (80, 100, 120, 255), 'online': (20, 170, 210, 255),
              'emitting': (240, 40, 10, 255), 'inactive': (70, 80, 90, 255),
              'active': (30, 220, 80, 255)}
    for key, color in colors.items():
        # Coordinate variations make UV orientation observable.
        pixels = bytes(c for y in range(4) for x in range(4)
                       for c in (min(255, color[0] + x * 3), min(255, color[1] + y * 3), color[2], 255))
        put(f'assets/fixturedata/textures/{key}.png', encode_png(4, 4, pixels))
    faces = {side: {'texture': '#surface', 'uv': [1, 2, 13, 15]}
             for side in ('down', 'up', 'north', 'south', 'west', 'east')}
    element = {'from': [2, 3, 4], 'to': [11, 13, 15], 'faces': faces}
    for key in ('offline', 'online', 'emitting'):
        put(f'assets/fixturedata/models/qio_{key}.json',
            {'textures': {'surface': 'fixturedata:' + key}, 'elements': [element]})
    wrapper = {'loader': 'mekanism:data_based', 'no_data': 'fixturedata:qio_online',
               'emitting': 'fixturedata:qio_emitting', 'textures': {'surface': 'fixturedata:emitting'}}
    put('assets/fixturedata/models/qio.json', wrapper)
    turns = {'up': (0, 0), 'down': (180, 0), 'north': (-90, 180),
             'south': (-90, 0), 'east': (-90, -90), 'west': (-90, 90)}
    variants = {f'active={active},facing={facing}': {'model': 'fixturedata:qio' if active == 'true' else 'fixturedata:qio_offline',
                                                  'x': x, 'y': y}
                for active in ('false', 'true') for facing, (x, y) in turns.items()}
    put('assets/fixturedata/blockstates/qio.json', {'variants': variants})
    # Portable base overrides remain separate resources, with their own scope.
    for key in ('inactive', 'active'):
        put(f'assets/refinedstorage/models/block/portable_grid/{key}.json',
            {'textures': {'surface': 'fixturedata:' + key}, 'elements': [element]})
    put('assets/fixturedata/models/portable.json', {'loader': 'refinedstorage:portable_grid',
                                                 'textures': {'surface': 'fixturedata:emitting'}})
    for name in ('portable_grid', 'creative_portable_grid'):
        put('assets/refinedstorage/blockstates/' + name + '.json',
            {'variants': {'': {'model': 'fixturedata:portable', 'x': 90, 'y': 180, 'ae2:z': 90, 'uvlock': True}}})
    return wrapper
