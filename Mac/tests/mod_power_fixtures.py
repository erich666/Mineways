"""Original asymmetric textures and geometry for static power-loader defaults."""
import json


def power_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for name, alpha in (('opaque',255),('transparent',128)):
        pixels=bytes(c for y in range(16) for x in range(16) for c in (16*x,16*y,70,alpha))
        put(f'assets/moderndynamics/textures/pipe/fixture_{name}/base.png',encode_png(16,16,pixels))
        put(f'assets/fixture/models/pipe_{name}.json', {
            'loader':'moderndynamics:pipe','pipeType':'fixture_'+name,'transparent':alpha!=255})
        put(f'assets/fixture/blockstates/pipe_{name}.json', {'variants':{
            'orientation=plain':{'model':'fixture:pipe_'+name},
            'orientation=turned':{'model':'fixture:pipe_'+name,'x':90,'y':270,'uvlock':True}}})
    for name, blue in (('frame',40),('led',90),('port',150)):
        pixels=bytes(c for y in range(16) for x in range(16) for c in (16*x,16*y,blue,255))
        put(f'assets/fixture/textures/power_{name}.png',encode_png(16,16,pixels))
    model={'loader':'mekanism:energy_cube','textures':{
        'frame':'fixture:power_frame','led':'fixture:power_led','port':'fixture:power_port'},
        'frame':[{'from':[2,3,4],'to':[14,13,12],'faces':{
            'north':{'texture':'#frame','uv':[1,2,13,12],'rotation':90}}}]}
    for relative, direction in [('bottom','down'),('top','up'),('front','north'),('back','south'),('left','east'),('right','west')]:
        model[relative+'LEDs']=[{'from':[6,6,6],'to':[10,10,10], 'faces':{
            direction:{'texture':'#led','uv':[3,4,9,10]}}}]
        model[relative+'Port']=[{'from':[0,0,0],'to':[16,16,16], 'faces':{
            direction:{'texture':'#port','uv':[0,0,16,16]}}}]
    put('assets/fixture/models/energy_cube.json',model)
    put('assets/fixture/blockstates/energy_cube.json',{'variants':{
        'facing=north':{'model':'fixture:energy_cube'},
        'facing=east':{'model':'fixture:energy_cube','y':90,'uvlock':True},
        'facing=up':{'model':'fixture:energy_cube','x':270}}})
    return model
