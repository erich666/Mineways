"""Original coordinate-coded chest sheets for the verified Twilight BERs."""
import json


FAMILIES = (('twilight_oak','twilight'),('canopy','canopy'),('mangrove','mangrove'),
            ('dark','darkwood'),('time','time'),('transformation','transformation'),
            ('mining','mining'),('sorting','sorting'))


def twilight_chest_assets(root, encode_png):
    def put(path, value):
        target = root/path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for index,(wood,material) in enumerate(FAMILIES):
        for trapped in (False,True):
            block = wood + ('_trapped_chest' if trapped else '_chest')
            put('assets/twilightforest/models/block/'+block+'.json',
                {'parent':'minecraft:builtin/entity','textures':{'particle':'fixture:missing_chest_particle'},
                 'transform':{'translation':[3,2,1]}})
            put('assets/twilightforest/blockstates/'+block+'.json',{'variants':{'':{
                'model':'twilightforest:block/'+block,'x':90,'y':270,'ae2:z':90,'uvlock':True}}})
            for shape_index,shape in enumerate(('single','left','right')):
                suffix = ('trapped' if shape=='single' else 'trapped_'+shape) if trapped else shape
                blue = 20 + 24*index + 3*shape_index + 12*trapped
                pixels = bytes(c for y in range(64) for x in range(64) for c in (4*x,4*y,blue,255))
                put('assets/twilightforest/textures/entity/chest/'+material+'/'+suffix+'.png',
                    encode_png(64,64,pixels))
