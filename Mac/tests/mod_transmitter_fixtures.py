"""Original disconnected transmitter geometry, with unwanted connection groups."""
import json


def transmitter_assets(root, encode_png):
    def put(path, data):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else json.dumps(data).encode())
    put('assets/fixture/textures/transmitter.png', encode_png(2, 2, bytes((180, 120, 60, 255)) * 4))
    put('assets/fixture/models/transmitter.mtl', b'newmtl core\nKd 0.5 1 0.25 1\nmap_Kd #core\nnewmtl hidden\nmap_Kd fixture:absent\n')
    obj = 'mtllib transmitter.mtl\n'
    points = [(4,4,4),(12,4,4),(12,12,4),(4,12,4),(4,4,12),(12,4,12),(12,12,12),(4,12,12)]
    obj += ''.join('v %g %g %g\n' % tuple(v/16 for v in point) for point in points)
    obj += 'vt 0 0.25\nvt 1 0.25\nvt 1 0.75\nvt 0 0.75\n'
    faces = {'down': (1,2,6,5), 'up': (4,8,7,3), 'north': (2,1,4,3),
             'south': (5,6,7,8), 'east': (6,2,3,7), 'west': (1,5,8,4)}
    for side, corners in faces.items():
        obj += 'o ' + side + 'NONE\nusemtl core\nf ' + ' '.join(f'{v}/{i+1}' for i,v in enumerate(corners)) + '\n'
    for group in ('downNORMAL', 'upPUSH', 'northPULL', 'extraNONE', 'DownNONE'):
        obj += 'o ' + group + '\nusemtl hidden\nf 1/1 2/2 3/3\n'
    put('assets/fixture/models/transmitter.obj.mek', obj.encode())
    model = {'loader': 'mekanism:transmitter', 'model': 'fixture:models/transmitter.obj.mek', 'flip_v': True,
             'textures': {'core': 'fixture:transmitter'},
             'glass': {'model': 'fixture:models/absent_glass.obj.mek'},
             'visibility': {'downNONE': False, 'downNORMAL': True}}
    put('assets/fixture/models/transmitter.json', model)
    definition = {'variants': {'facing=north': {'model': 'fixture:transmitter'},
                               'facing=east': {'model': 'fixture:transmitter', 'y': 90}}}
    put('assets/fixture/blockstates/transmitter.json', definition)
    return model, definition
