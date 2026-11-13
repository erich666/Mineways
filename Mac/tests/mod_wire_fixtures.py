"""Original static wire parts and the verified empty rack resource contract."""
import json


def wire_assets(root, encode_png):
    def put(path, data):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else json.dumps(data).encode())
    put('assets/fixture/textures/wire.png', encode_png(2, 2, bytes((190,45,30,255))*4))
    faces = {side: {'texture': 'fixture:wire', 'uv': [0,0,16,16], 'tintindex': 1}
             for side in ('down','up','north','south','west','east')}
    for name, start, end in [('node',[6,0,6],[10,2,10]), ('elbow',[0,0,6],[6,2,10])]:
        put('assets/fixture/models/wire_' + name + '.json', {'loader': 'morered:rotate_tints',
            'elements': [{'from': start, 'to': end, 'faces': faces}]})
    put('assets/fixture/models/wire_parts.json', {'loader': 'morered:wire_parts',
        'line': {'parent':'fixture:unavailable_connection_line'},
        'edge': {'parent':'fixture:unavailable_connection_edge'}})
    definition = {'multipart': [
        {'apply': {'model':'fixture:wire_parts'}},
        {'when': {'down':True}, 'apply': {'model':'fixture:wire_node'}},
        {'when': {'west':True}, 'apply': {'model':'fixture:wire_node','x':90,'y':90}},
        {'when': {'down':True,'west':True}, 'apply': {'model':'fixture:wire_elbow'}}]}
    put('assets/fixture/blockstates/wire.json', definition)
    put('assets/minecolonies/models/block/blockairrack.json', {'textures': {'particle':'minecraft:block/oak_log'}})
    put('assets/minecolonies/blockstates/blockminecoloniesrack.json', {'variants': {
        'facing=north,variant=blockrackair': {'model':'minecolonies:block/blockairrack'},
        'facing=east,variant=blockrackair': {'model':'minecolonies:block/blockairrack','y':90}}})
    return definition
