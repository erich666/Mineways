"""Original coordinate-coded sheets for eight Twilight sign woods."""
import json

WOODS = ('twilight_oak','canopy','mangrove','dark','time','transformation','mining','sorting')


def twilight_sign_assets(root, encode_png):
    def put(path, value):
        target = root/path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for index,wood in enumerate(WOODS):
        pixels = bytes(c for y in range(32) for x in range(64) for c in (4*x,8*y,30+24*index,255))
        put('assets/twilightforest/textures/entity/signs/'+wood+'.png',encode_png(64,32,pixels))
        for wall in (False,True):
            name = (('twilight' if wood=='twilight_oak' else wood)+'_wall_sign') if wall else wood+'_sign'
            put('assets/twilightforest/models/block/'+name+'.json',
                {'parent':'minecraft:builtin/entity','textures':{'particle':'fixture:missing_sign_particle'},
                 'transform':{'translation':[3,2,1]},'visibility':{'board':False}})
            put('assets/twilightforest/blockstates/'+name+'.json',{'variants':{'':{
                'model':'twilightforest:block/'+name,'x':90,'y':270,'ae2:z':90,'uvlock':True}}})

    for index,wood in enumerate(WOODS):
        pixels = bytes(c for y in range(32) for x in range(64) for c in (4*x,8*y,40+24*index,255))
        put('assets/twilightforest/textures/entity/signs/hanging/'+wood+'.png',encode_png(64,32,pixels))
        for wall in (False,True):
            name = wood + ('_wall_hanging_sign' if wall else '_hanging_sign')
            put('assets/twilightforest/models/block/'+name+'.json',
                {'parent':'minecraft:builtin/entity','textures':{'particle':'fixture:missing_sign_particle'},
                 'transform':{'translation':[3,2,1]},'visibility':{'board':False}})
            put('assets/twilightforest/blockstates/'+name+'.json',{'variants':{'':{
                'model':'twilightforest:block/'+name,'x':90,'y':270,'ae2:z':90,'uvlock':True}}})
