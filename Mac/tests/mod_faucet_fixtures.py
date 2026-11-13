"""Original dry faucet bodies and an unrelated liquid mesh for exclusion tests."""
import json


def faucet_assets(root, encode_png):
    def put(path, value):
        target = root/path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for label, blue in (('off',70),('on',150),('liquid',230)):
        pixels = bytes(c for y in range(16) for x in range(16) for c in (16*x,16*y,blue,255))
        put('assets/fixture/textures/faucet_'+label+'.png',encode_png(16,16,pixels))
    models = {}
    for label, low, high in (('off',[2,5,3],[12,11,14]),('on',[3,4,2],[14,12,13]),
                             ('liquid',[6,-8,6],[10,5,10])):
        faces = {direction:{'texture':'#body','uv':[1,2,13,15]}
                 for direction in ('down','up','north','south','west','east')}
        faces['up']['rotation'] = 90
        faces['north']['cullface'] = 'north'
        models[label] = {'textures':{'body':'fixture:faucet_'+label},
                         'elements':[{'from':low,'to':high,'faces':faces}]}
        put('assets/fixture/models/faucet_body_'+label+'.json',models[label])
    for label in ('off','on'):
        wrapper = {'loader':'supplementaries:faucet','model':'fixture:faucet_body_'+label,
                   'liquid':'fixture:faucet_body_liquid','textures':{'body':'fixture:missing_outer'},
                   'visibility':{'body':False},'transform':{'translation':[4,3,2]}}
        put('assets/fixture/models/faucet_'+label+'.json',wrapper)
        variants = {'facing='+facing+',pose=plain':{'model':'fixture:faucet_'+label,'y':turn}
                    for facing,turn in (('north',0),('east',90),('south',180),('west',270))}
        variants['facing=north,pose=turned'] = {'model':'fixture:faucet_'+label,'x':90,'y':270,'uvlock':True}
        put('assets/fixture/blockstates/faucet_'+label+'.json',{'variants':variants})
    return models
