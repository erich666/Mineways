"""Original asymmetric XyCraft child models, with independent core/cap poses."""
import json


def xycraft_pipe_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    pixels = bytes(c for y in range(16) for x in range(16) for c in (x * 16, y * 16, 110, 255))
    put('assets/fixture/textures/xycraft_pipe.png', encode_png(16, 16, pixels))
    put('assets/fixture/textures/xycraft_core.png', encode_png(2, 2, bytes((190, 70, 25, 255)) * 4))
    core = {'textures': {'body': 'fixture:xycraft_core'}, 'elements': [
        {'from': [6, 6, 5], 'to': [10, 11, 10], 'faces': {
            'north': {'texture': '#body', 'uv': [1, 2, 12, 14], 'cullface': 'north'},
            'up': {'texture': '#body', 'uv': [2, 3, 14, 15], 'rotation': 90}}}]}
    cap = {'textures': {'cap': 'fixture:xycraft_pipe'}, 'elements': [
        {'from': [3, 3, 5], 'to': [11, 5, 13], 'faces': {
            'down': {'texture': '#cap', 'uv': [1, 2, 14, 13], 'rotation': 90, 'cullface': 'down'}}},
        {'from': [4, 3, 6], 'to': [6, 4, 8],
         'rotation': {'origin': [5, 4, 7], 'axis': 'z', 'angle': 22.5, 'rescale': True},
         'faces': {'east': {'texture': '#cap', 'uv': [3, 4, 12, 15]}}}]}
    put('assets/fixture/models/xycraft_core.json', core)
    put('assets/fixture/models/xycraft_cap.json', cap)
    wrapper = {'loader': 'xycraft_machines:pipe',
               'core': {'parent': 'fixture:xycraft_core'}, 'cap': {'parent': 'fixture:xycraft_cap'}}
    for key in ('straight', 'extract', 'insert', 'short_straight'):
        wrapper[key] = {'parent': 'fixture:xycraft_core'}
    put('assets/fixture/models/xycraft_pipe.json', wrapper)
    put('assets/fixture/blockstates/xycraft_pipe.json', {'variants': {
        'pose=plain': {'model': 'fixture:xycraft_pipe'},
        'pose=turned': {'model': 'fixture:xycraft_pipe', 'x': 90, 'y': 270, 'uvlock': True},
        'pose=rolled': {'model': 'fixture:xycraft_pipe', 'ae2:z': 90}}})
    # One down-facing rectangle. Hidden component refers to a missing texture.
    obj = ('mtllib xycraft_cap.mtl\n'
           'v 0.1875 0.1875 0.3125\nv 0.6875 0.1875 0.3125\n'
           'v 0.6875 0.1875 0.8125\nv 0.1875 0.1875 0.8125\n'
           'vt 0.0625 0.125\nvt 0.875 0.125\nvt 0.875 0.8125\nvt 0.0625 0.8125\n'
           'g Cap\nusemtl cap\nf 1/1 2/2 3/3 4/4\n'
           'g Hidden\nusemtl hidden\nf 1/1 2/2 3/3 4/4\n')
    put('assets/fixture/models/xycraft_cap.obj', obj.encode())
    put('assets/fixture/models/xycraft_cap.mtl', b'newmtl cap\nmap_Kd fixture:xycraft_pipe\nnewmtl hidden\nmap_Kd fixture:missing_hidden\n')
    obj_wrapper = {**wrapper, 'cap': {'loader': 'neoforge:composite', 'children': {'cap': {
        'loader': 'neoforge:obj', 'model': 'fixture:models/xycraft_cap.obj',
        'flip_v': True, 'visibility': {'Hidden': False}}}}}
    put('assets/fixture/models/xycraft_pipe_obj.json', obj_wrapper)
    put('assets/fixture/blockstates/xycraft_pipe_obj.json', {'variants': {'': {'model': 'fixture:xycraft_pipe_obj'}}})
    return wrapper, core, cap
