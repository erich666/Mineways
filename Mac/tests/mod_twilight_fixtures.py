"""Original asymmetric pixels and conditional source faces for Twilight tests."""
import json


def twilight_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for name, pixels in (
            ('twilight_base', bytes(c for y in range(16) for x in range(16) for c in (x*16, y*16, 40, 255))),
            ('twilight_overlay', bytes(c for y in range(16) for x in range(16)
                                       for c in (200, 160, 80, 255 if x < 8 else 0)))):
        put('assets/fixture/textures/' + name + '.png', encode_png(16, 16, pixels))
    connected = {'loader': 'twilightforest:connected_texture_block',
                 'textures': {'base_texture': 'fixture:twilight_base', 'overlay_texture': 'fixture:twilight_overlay'},
                 'connectable_blocks': ['fixture:twilight_connected']}
    put('assets/fixture/models/twilight_connected.json', connected)
    put('assets/fixture/blockstates/twilight_connected.json', {'variants': {
        'pose=plain': {'model': 'fixture:twilight_connected'},
        'pose=turned': {'model': 'fixture:twilight_connected', 'x': 90, 'y': 270, 'uvlock': True}}})
    put('assets/fixture/models/twilight_pink.json', {**connected, 'connected_texture': {'tint_index': 0}})
    put('assets/twilightforest/blockstates/pink_castle_door.json', {'variants': {'': {'model': 'fixture:twilight_pink'}}})
    # Each condition has deliberately asymmetric bounds and UVs. Root rotation
    # and element rotation must be ignored by the shipped force-field path.
    force = {'loader': 'twilightforest:force_field', 'textures': {'pane': 'fixture:twilight_base'}, 'elements': [
        {'from': [7, 6, 5], 'to': [9, 10, 11], 'faces': {'north': {'texture': '#pane', 'uv': [2, 3, 13, 14]}},
         'condition': {'direction': 'north', 'if': False, 'parents': []}},
        {'from': [7, 6, 0], 'to': [9, 10, 5], 'faces': {'north': {'texture': '#pane', 'uv': [1, 4, 12, 15]}},
         'condition': {'direction': 'north', 'if': True, 'parents': []},
         'rotation': {'axis': 'x', 'angle': 22.5, 'origin': [8, 8, 8]}},
        {'from': [9, 6, 5], 'to': [16, 10, 11], 'faces': {'up': {'texture': '#pane', 'uv': [0, 2, 15, 13]}},
         'condition': {'direction': 'north_east', 'if': False, 'parents': ['east']}},
        {'from': [9, 6, 0], 'to': [16, 10, 5], 'faces': {'up': {'texture': 'fixture:missing_diagonal'}},
         'condition': {'direction': 'north_east', 'if': True, 'parents': ['north', 'east']}}]}
    put('assets/fixture/models/twilight_forcefield.json', force)
    put('assets/fixture/blockstates/twilight_forcefield.json', {'variants': {
        '': {'model': 'fixture:twilight_forcefield', 'y': 90, 'uvlock': True}}})
    return connected, force
