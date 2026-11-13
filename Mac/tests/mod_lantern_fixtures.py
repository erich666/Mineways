"""Original asymmetric wall lantern assets for static-body transform checks."""
import json


def lantern_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    put('assets/fixture/textures/lantern_body.png', encode_png(2, 2, bytes((
        210,40,30,255, 35,185,65,255, 45,70,220,255, 235,205,45,255))))
    put('assets/fixture/textures/lantern_support.png', encode_png(2,2,bytes((80,110,140,255))*4))
    faces = {side: {'texture': 'fixture:lantern_body'}
             for side in ('down','up','north','south','west','east')}
    faces['up'].update(uv=[1,2,13,14], rotation=90)
    body = {'elements': [
        {'from':[3,1,5], 'to':[11,10,13], 'faces':faces},
        {'from':[6,4,8], 'to':[8,6,10],
         'rotation':{'axis':'z','angle':22.5,'origin':[7,5,9],'rescale':True},
         'faces':{'north':{'texture':'fixture:lantern_body','uv':[2,3,7,11]},
                  'up':{'texture':'fixture:lantern_body','uv':[4,5,8,13]}}}]}
    put('assets/minecraft/models/block/lantern.json', body)
    put('assets/minecraft/blockstates/lantern.json', {'variants': {
        'hanging=false': {'model':'minecraft:block/lantern'},
        'hanging=true': {'model':'fixture:unavailable_hanging_body'}}})
    put('assets/fixture/models/lantern_support.json', {'elements':[
        {'from':[1,12,13], 'to':[15,15,16], 'faces':{
            'north':{'texture':'fixture:lantern_support','uv':[0,0,16,16]},
            'up':{'texture':'fixture:lantern_support','uv':[0,0,16,16]}}}]})
    put('assets/amendments/models/block/wall_lantern.json', {
        'loader':'amendments:wall_lantern','support':'fixture:lantern_support'})
    definition = {'variants':{'facing='+direction+',attachment=block': {
        'model':'amendments:block/wall_lantern','y':turn}
        for direction, turn in [('north',0),('east',90),('south',180),('west',270)]}}
    put('assets/amendments/blockstates/wall_lantern.json', definition)
    return definition, body
