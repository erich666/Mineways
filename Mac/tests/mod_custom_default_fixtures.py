"""Original art for framed children and microchip directional defaults."""
import json


def custom_default_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for name, color in {'framed_front': (210, 70, 35, 255), 'framed_side': (30, 130, 195, 255),
                        'microchip_base': (95, 175, 45, 255), 'microchip_top': (180, 60, 185, 255)}.items():
        put(f'assets/fixture/textures/{name}.png', encode_png(2, 2, bytes(color) * 4))
    front = {'textures': {'surface': 'fixture:framed_front'}, 'elements': [
        {'from': [1, 2, 3], 'to': [15, 14, 13], 'faces': {'north': {'texture': '#surface', 'uv': [1, 2, 15, 14]}}}]}
    side = {'textures': {'surface': 'fixture:framed_side'}, 'elements': [
        {'from': [1, 2, 3], 'to': [15, 14, 13], 'faces': {'up': {'texture': '#surface'}, 'east': {'texture': '#surface'}}}]}
    put('assets/fixture/models/framed_front.json', front)
    put('assets/fixture/models/framed_side.json', side)
    put('assets/fixture/models/framed_default.json', {
        'loader': 'functionalstorage:framedblock', 'textures': {'surface': 'fixture:missing'},
        'children': {'front': {'parent': 'fixture:framed_front'}, 'side': {'parent': 'fixture:framed_side'}},
        'item_render_order': ['front']})
    put('assets/fixture/blockstates/framed_default.json', {'variants': {
        'facing=north': {'model': 'fixture:framed_default'},
        'facing=east': {'model': 'fixture:framed_default', 'y': 90, 'uvlock': True}}})
    put('assets/fixture/models/microchip_default.json', {'loader': 'little_big_redstone:microchip',
        'textures': {'base': 'fixture:microchip_base', 'base_up': '#top', 'top': 'fixture:microchip_top',
                     'signal_on_overlay': 'fixture:missing'}})
    put('assets/fixture/blockstates/microchip_default.json', {'variants': {
        'facing=north': {'model': 'fixture:microchip_default'},
        'facing=east': {'model': 'fixture:microchip_default', 'x': 90, 'y': 270, 'uvlock': True}}})
    return front, side
