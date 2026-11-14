"""Original directional gradients for IE configurable-side export tests."""
import json


def ie_sides_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    base = 'fixture:block/ie_sides'
    for suffix, blue in (('down_none', 40), ('up_none', 90), ('side_none', 150), ('side', 200)):
        pixels = bytes(channel for y in range(16) for x in range(16)
                       for channel in (x * 16, y * 16, blue, 255))
        put('assets/fixture/textures/block/ie_sides_' + suffix + '.png', encode_png(16, 16, pixels))
    for kind in ('side_top_bottom', 'side_vertical', 'vertical', 'all_same_texture'):
        name = 'ie_sides_' + kind
        model = {'loader': 'immersiveengineering:conf_sides', 'type': kind, 'base_name': base}
        put('assets/fixture/models/' + name + '.json', model)
        put('assets/fixture/blockstates/' + name + '.json', {'variants': {
            'orientation=plain': {'model': 'fixture:' + name},
            'orientation=turned': {'model': 'fixture:' + name, 'x': 90, 'y': 270, 'uvlock': True}}})
