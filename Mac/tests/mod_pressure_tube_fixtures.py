"""Original source models and pixel gradients for pressure-tube export tests."""
import json


def pressure_tube_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    pixels = bytes(channel for y in range(16) for x in range(16) for channel in (x*16, y*16, 90, 255))
    put('assets/fixture/textures/pressure_tube.png', encode_png(16, 16, pixels))
    # Six rotated copies of this asymmetric plate. The north face is deliberately
    # culled and therefore never returned by PressureTubeModel's null-side query.
    source = {'textures': {'pipe':'fixture:pressure_tube'}, 'elements': [
        {'from':[3,10,5], 'to':[11,12,13], 'faces': {
            'up': {'texture':'#pipe', 'uv':[1,2,14,13], 'rotation':90},
            'north': {'texture':'fixture:missing_culled', 'cullface':'north'}}},
        {'from':[4,11,6], 'to':[6,12,8],
         'rotation': {'origin':[5,11,7], 'axis':'z', 'angle':22.5, 'rescale':True},
         'faces': {'east': {'texture':'#pipe', 'uv':[3,4,12,15]}}}]}
    put('assets/pneumaticcraft/models/block/test_pressure_tube_disconnected.json', source)
    put('assets/fixture/models/pressure_tube.json', {'loader':'pneumaticcraft:pressure_tube','prefix':'test'})
    put('assets/fixture/blockstates/pressure_tube.json', {'variants': {
        'pose=plain': {'model':'fixture:pressure_tube'},
        'pose=turned': {'model':'fixture:pressure_tube', 'x':90, 'y':270, 'uvlock':True},
        'pose=rolled': {'model':'fixture:pressure_tube', 'ae2:z':90}}})
    return source
