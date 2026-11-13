"""Original coordinate-coded chest sheets and inherited per-half materials."""
import json


def chest_assets(root, encode_png):
    def put(path, value):
        path = root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    materials = {}
    for index, shape in enumerate(('single','left','right')):
        base = bytes(c for y in range(64) for x in range(64) for c in (x*4,y*4,40+index*40,255))
        put(f'assets/fixturechest/textures/{shape}_base.png', encode_png(64,64,base))
        # Parent's base is intentionally absent: the child definition must replace it.
        entries = {'base':'fixturechest:absent_parent_base'}
        for tier_index, tier in enumerate(('wood','copper','iron','gold','diamond','netherite')):
            pixels = bytes(c for y in range(64) for x in range(64)
                           for c in ((30+tier_index*35,210,90+index*40,255)
                                     if (x<8 and y<8) or (x in (20,21) and y==20) else (0,0,0,0)))
            texture = f'fixturechest:{shape}_{tier}'
            put(f'assets/fixturechest/textures/{shape}_{tier}.png', encode_png(64,64,pixels))
            entries[tier+'_tier'] = texture
        materials[shape] = entries
    put('assets/fixturechest/storage_texture_definitions/chest.json', {'type':'chest','textures':materials})
    put('assets/sophisticatedstorage/storage_texture_definitions/acacia_chest.json',
        {'parent':'fixturechest:chest','textures':{shape:{'base':f'fixturechest:{shape}_base'} for shape in materials}})
    model = {'loader':'sophisticatedstorage:chest'}
    put('assets/sophisticatedstorage/models/block/chest.json', model)
    for tier in ('wood','copper','iron','gold','diamond','netherite'):
        name = (tier+'_' if tier!='wood' else '')+'chest'
        put('assets/sophisticatedstorage/blockstates/'+name+'.json',
            {'variants':{'':{'model':'sophisticatedstorage:block/chest','x':90,'y':270,'uvlock':True}}})
    return model
