"""Original asymmetric MI defaults for native identity-rotation regressions."""
import json


def machine_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for name, color in (('machine_surface', (55, 125, 185, 255)), ('machine_overlay', (185, 105, 35, 255))):
        put(f'assets/fixture/textures/{name}.png', encode_png(2, 2, bytes(color) * 4))
    put('assets/fixture/models/machine_shell.json', {'elements': [
        {'from': [1, 2, 3], 'to': [14, 15, 16], 'faces': {
            'north': {'texture': 'fixture:machine_surface', 'uv': [1, 2, 14, 15]},
            'up': {'texture': 'fixture:machine_surface', 'uv': [1, 3, 14, 16]}}}]})
    put('assets/fixture/blockstates/machine_shell.json', {'variants': {'': {'model': 'fixture:machine_shell'}}})
    delegate = {'loader': 'modern_industrialization:use_block_model', 'block': 'fixture:machine_shell'}
    put('assets/fixture/models/machine_casing/test.json', delegate)
    put('assets/fixture/models/machine_default.json', {'loader': 'modern_industrialization:machine',
        'casing': 'fixture:test', 'default_overlays': {'front': 'fixture:machine_overlay'}})
    put('assets/fixture/models/machine_delegate.json', delegate)
    for name in ('machine_default', 'machine_delegate'):
        put(f'assets/fixture/blockstates/{name}.json', {'variants': {
            'facing=north': {'model': 'fixture:' + name},
            'facing=east': {'model': 'fixture:' + name, 'x': 90, 'y': 270, 'uvlock': True}}})

    put('assets/minecraft/models/block/air.json', {})
    for block in ('modern_industrialization:hydrogen', 'extended_industrialization:honey'):
        namespace, name = block.split(':')
        put(f'assets/{namespace}/blockstates/{name}.json', {'variants': {'': {'model': 'minecraft:block/air'}}})
