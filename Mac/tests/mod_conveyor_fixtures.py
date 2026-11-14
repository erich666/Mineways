"""Original asymmetric textures exercise the fixed procedural conveyor meshes."""
import json


def conveyor_assets(root, encode_png):
    def put(path, value):
        target = root/path
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(value if isinstance(value,bytes) else json.dumps(value).encode())
    for image, blue in (('off',35),('dropper_off',85),('vertical_off',235),('casing_side',135),('casing_walls',185)):
        pixels = bytes(c for y in range(16) for x in range(16) for c in (16*x,16*y,blue,255))
        put('assets/immersiveengineering/textures/block/conveyor/'+image+'.png',encode_png(16,16,pixels))
    for kind in ('basic','dropper','vertical'):
        name = 'conveyor_'+kind
        put('assets/immersiveengineering/models/block/'+name+'.json', {
            'loader':'immersiveengineering:models/conveyor','conveyorType':'immersiveengineering:'+kind,
            'transform':{'translation':[3,2,1]},'textures':{'ignored':'fixture:missing_conveyor'},
            'visibility':{'ignored':False}})
        put('assets/immersiveengineering/blockstates/'+name+'.json', {'variants':{'':{
            'model':'immersiveengineering:block/'+name,'x':90,'y':270,'ae2:z':90,'uvlock':True}}})
