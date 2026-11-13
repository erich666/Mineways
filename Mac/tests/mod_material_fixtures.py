"""Original material-model assets shared by importer and native export tests."""
import json


def material_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    put('assets/fixture/textures/material_default.png', encode_png(2, 2, bytes((160, 65, 195, 255)) * 4))
    put('assets/fixture/textures/material_outer.png', encode_png(2, 2, bytes((255, 0, 0, 255)) * 4))
    parent = {'textures': {'surface': 'fixture:material_default'}, 'elements': [
        {'from': [1, 2, 3], 'to': [12, 14, 15], 'faces': {
            'north': {'texture': '#surface', 'uv': [2, 3, 13, 15], 'rotation': 90},
            'up': {'texture': '#surface', 'uv': [1, 2, 12, 14]}}}]}
    put('assets/fixture/models/material_parent.json', parent)
    put('assets/fixture/models/material_empty_parent.json', {'textures': {'particle': 'fixture:material_default'}, 'elements': []})
    loader = 'domum_ornamentum:materially_textured'
    put('assets/fixture/models/material_wrapper.json', {'loader': loader, 'parent': 'fixture:material_parent',
                                                       'textures': {'surface': 'fixture:material_outer'}})
    put('assets/fixture/models/material_empty.json', {'loader': loader, 'parent': 'fixture:material_empty_parent'})
    put('assets/fixture/blockstates/material_block.json', {'variants': {
        'facing=north': {'model': 'fixture:material_wrapper'},
        'facing=east': {'model': 'fixture:material_wrapper', 'x': 90, 'y': 270, 'uvlock': True},
        'facing=up': {'model': 'fixture:material_empty'}}})
    put('assets/fixture/blockstates/material_multipart.json', {'multipart': [
        {'apply': {'model': 'fixture:material_wrapper'}},
        {'when': {'connected': 'false'}, 'apply': {'model': 'fixture:material_empty'}},
        {'when': {'connected': 'true'}, 'apply': {'model': 'fixture:material_wrapper', 'y': 90}}]})
    return parent
