"""Original Framework components shared by Python and native export checks."""
import json


def framework_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    put('assets/fixture/textures/framework_parent.png', encode_png(2, 2, bytes((255, 0, 0, 255)) * 4))
    put('assets/fixture/textures/framework_override.png', encode_png(2, 2, bytes((35, 145, 215, 255)) * 4))
    component = {'from': [2, 3, 4], 'to': [13, 11, 14],
                 'rotation': {'axis': 'x', 'angle': 11.31, 'origin': [4, 5, 6]},
                 'faces': {side: {'texture': '#surface', 'uv': [1, 2, 13, 14]}
                           for side in ('down', 'up', 'north', 'south', 'west', 'east')}}
    parent = {'loader': 'framework:open_model', 'components': [component],
              'textures': {'surface': 'fixture:framework_parent'}}
    put('assets/fixture/models/framework_parent.json', parent)
    put('assets/fixture/models/framework_child.json', {'parent': 'fixture:framework_parent',
        'textures': {'surface': 'fixture:framework_override'}})
    put('assets/fixture/blockstates/framework_open.json', {'variants': {
        'facing=north': {'model': 'fixture:framework_child'},
        'facing=east': {'model': 'fixture:framework_child', 'y': 90, 'uvlock': True}}})
    return parent
