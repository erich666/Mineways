"""Original 64x32 canvas-sign textures expose every UV axis and color mapping."""
import json


def canvas_sign_assets(root, encode_png):
    def put(path, value):
        target = root/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(value if isinstance(value,bytes) else json.dumps(value).encode())
    for color,blue in (('',60),('red',180)):
        pixels = bytes(c for y in range(32) for x in range(64) for c in (4*x,8*y,blue,255))
        put('assets/farmersdelight/textures/entity/signs/canvas'+('_'+color if color else '')+'.png',encode_png(64,32,pixels))
        for kind in ('canvas_sign','canvas_wall_sign'):
            name = (color+'_' if color else '')+kind
            put('assets/farmersdelight/blockstates/'+name+'.json',{'variants':{'':{
                'model':'farmersdelight:block/canvas_sign','x':90,'y':270,'uvlock':True}}})
    put('assets/farmersdelight/models/block/canvas_sign.json',{'textures':{'particle':'fixture:missing_sign_particle'},
        'transform':{'translation':[3,2,1]},'visibility':{'post':False}})
