"""Original 64x64 coordinate colors and tier overlays for closed shulker tests."""
import json


def shulker_assets(root, encode_png):
    def put(path, value):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    base = bytes(c for y in range(64) for x in range(64) for c in (x * 4, y * 4, 60, 255))
    put('assets/sophisticatedstorage/textures/entity/shulker_box/no_tint.png', encode_png(64, 64, base))
    for index, tier in enumerate(('base', 'copper', 'iron', 'gold', 'diamond', 'netherite')):
        overlay = bytes(c for y in range(64) for x in range(64)
                        for c in ((20 + index * 35, 220, 180, 255) if x in (20, 21) and y == 20 and index else (0, 0, 0, 0)))
        put('assets/sophisticatedstorage/textures/entity/shulker_box/' + tier + '_tier.png', encode_png(64, 64, overlay))
        block = (tier + '_' if tier != 'base' else '') + 'shulker_box'
        put('assets/sophisticatedstorage/blockstates/' + block + '.json',
            {'variants': {'': {'model': 'sophisticatedstorage:block/shulker_box'}}})
    model = {'loader': 'sophisticatedstorage:shulker_box'}
    put('assets/sophisticatedstorage/models/block/shulker_box.json', model)
    return model
