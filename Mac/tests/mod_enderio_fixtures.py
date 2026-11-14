"""Original machine art with the inspected EnderIO overlay component."""
import json


def enderio_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    put('assets/fixture/textures/io_machine.png', encode_png(2, 2, bytes((45, 170, 120, 255)) * 4))
    parent = {'textures': {'surface': 'fixture:io_machine'}, 'elements': [
        {'from': [2, 1, 3], 'to': [14, 15, 12], 'faces': {
            'north': {'texture': '#surface', 'uv': [2, 1, 14, 15], 'rotation': 90},
            'up': {'texture': '#surface', 'uv': [2, 3, 14, 12]},
            'east': {'texture': '#surface', 'uv': [3, 1, 12, 15]}}}]}
    put('assets/fixture/models/io_machine_base.json', parent)
    put('assets/fixture/models/io_overlay.json', {'loader': 'enderio:io_overlay'})
    put('assets/fixture/models/io_machine.json', {'loader': 'neoforge:composite', 'children': {
        'overlay': {'parent': 'fixture:io_overlay'}, 'block': {'parent': 'fixture:io_machine_base'}}})
    put('assets/fixture/blockstates/io_machine.json', {'variants': {
        'facing=north': {'model': 'fixture:io_machine'},
        'facing=east': {'model': 'fixture:io_machine', 'y': 90, 'uvlock': True}}})
    put('assets/fixture/blockstates/io_overlay_only.json', {'variants': {'': {'model': 'fixture:io_overlay'}}})
    return parent
