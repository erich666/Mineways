"""Original shared storage assets for importer and native export regressions."""
import json


def put(root, path, data):
    path = root / path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else json.dumps(data).encode())


def original_model(texture):
    return {'textures': {'surface': texture}, 'elements': [
        {'from': [2, 1, 3], 'to': [13, 14, 15], 'faces': {
            'north': {'texture': '#surface', 'uv': [2, 3, 11, 12], 'rotation': 90, 'cullface': 'north'},
            'up': {'texture': '#surface', 'uv': [1, 4, 13, 15]}}},
        {'from': [3, 4, 5], 'to': [6, 7, 8], 'faces': {'east': {'texture': '#surface', 'uv': [4, 5, 7, 8]}}}]}


def simple_material_assets(root, encode_png):
    """A shared fixture with distinct base, overlay, and resource override art."""
    for name, color in [('base', (50, 110, 170, 255)), ('overlay', (255, 0, 0, 255))]:
        put(root, f'assets/fixture/textures/{name}.png', encode_png(2, 2, bytes(color) * 4))
    base = original_model('fixture:base')
    base['elements'][1]['rotation'] = {'origin': [4, 5, 6], 'axis': 'z', 'angle': 22.5, 'rescale': True}
    put(root, 'assets/fixture/models/storage_parent.json', base)
    source = {'loader': 'sophisticatedstorage:simple_material',
              'textures': {'surface': 'fixture:overlay'},
              'base': {'parent': 'fixture:storage_parent'},
              'overlay': original_model('fixture:overlay')}
    put(root, 'assets/fixture/models/simple_storage.json', source)
    put(root, 'assets/fixture/blockstates/simple_storage.json', {'variants': {
        'facing=north': {'model': 'fixture:simple_storage'},
        'facing=east': {'model': 'fixture:simple_storage', 'x': 90, 'y': 270, 'uvlock': True}}})
    return source



def limited_barrel_assets(root, encode_png):
    """Original recessed/flat barrel parts, with texture-only tier inheritance."""
    for name, color in [('wood', (35, 125, 195, 255)), ('tier', (210, 145, 25, 255)),
                        ('unused', (255, 0, 0, 255))]:
        put(root, f'assets/fixture/textures/barrel_{name}.png', encode_png(2, 2, bytes(color) * 4))
    for name, start, end in [('recessed', [1, 0, 2], [15, 14, 16]),
                              ('flat', [0, 0, 0], [16, 16, 16]),
                              ('trim', [2, 14, 3], [14, 16, 15])]:
        put(root, f'assets/fixture/models/barrel_{name}.json', {'elements': [
            {'from': start, 'to': end, 'faces': {'north': {'texture': '#surface', 'uv': [1, 2, 13, 14]},
                                               'up': {'texture': '#surface', 'uv': [2, 3, 14, 15], 'rotation': 90}}}]})
    loader = 'sophisticatedstorage:limited_barrel'
    parent = {'loader': loader, 'model_parts': {
        'base': {'model': 'fixture:barrel_recessed', 'textures': {'surface': 'fixture:barrel_unused'}},
        'tier': {'model': 'fixture:barrel_trim', 'textures': {'surface': 'fixture:barrel_unused'}}},
        'wood_overrides': {'acacia': {'base': {'textures': {'surface': 'fixture:barrel_wood'}}},
                           'oak': {'base': {'textures': {'surface': 'fixture:barrel_unused'}}}}}
    put(root, 'assets/fixture/models/barrel_parent.json', parent)
    tier = {'loader': loader, 'parent': 'fixture:barrel_parent',
            'model_parts': {'tier': {'textures': {'surface': 'fixture:barrel_tier'}}}}
    put(root, 'assets/fixture/models/limited_barrel.json', tier)
    flat = {'loader': loader, 'parent': 'fixture:limited_barrel',
            'model_parts': {'base': {'model': 'fixture:barrel_flat'}}}
    put(root, 'assets/fixture/models/limited_barrel_flat.json', flat)
    definition = {'variants': {
        'facing=north,flat_top=false,vertical_facing=up': {'model': 'fixture:limited_barrel'},
        'facing=east,flat_top=false,vertical_facing=no': {'model': 'fixture:limited_barrel', 'x': 90, 'y': 90},
        'facing=west,flat_top=true,vertical_facing=down': {'model': 'fixture:limited_barrel_flat', 'x': 180, 'y': 270}}}
    put(root, 'assets/fixture/blockstates/limited_barrel.json', definition)
    return parent, tier, flat, definition
