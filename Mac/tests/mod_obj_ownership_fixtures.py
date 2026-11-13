"""Original asymmetric OBJ assets exposing loader/context inheritance separately."""
import json


def obj_ownership_assets(root, encode_png):
    def put(path, value):
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    for label in ('inherited','reset'):
        pixels = bytes(c for y in range(16) for x in range(16)
                       for c in ((16*x,16*y,40,255) if label=='inherited' else (40,16*x,16*y,255)))
        put('assets/fixture/textures/obj_owned_' + label + '.png', encode_png(16,16,pixels))
    put('assets/fixture/models/obj_owned_default.mtl', b'newmtl surface\nmap_Kd #surface\nnewmtl hidden\nmap_Kd fixture:unavailable_hidden\n')
    put('assets/fixture/models/obj_owned_override.mtl', b'newmtl surface\nmap_Kd #override\nnewmtl hidden\nmap_Kd fixture:unavailable_hidden\n')
    for label, corners in [('parent',[(14,3,4),(2,3,4),(2,13,4),(14,13,4)]),
                           ('reset',[(3,1,12),(11,1,12),(11,9,12),(3,9,12)])]:
        text = 'mtllib obj_owned_default.mtl\n'
        text += ''.join('v %g %g %g\n' % tuple(value/16 for value in point) for point in corners)
        text += 'vt 0.125 0.25\nvt 0.75 0.25\nvt 0.75 0.875\nvt 0.125 0.875\n'
        text += 'o panel\nusemtl surface\nf 1/1 2/2 3/3 4/4\n'
        text += 'o hidden\nusemtl hidden\nf 1/1 2/2 3/3\n'
        put('assets/fixture/models/obj_owned_' + label + '.obj', text.encode())
    parent = {'loader':'neoforge:obj','model':'fixture:models/obj_owned_parent.obj',
              'automatic_culling':False,'shade_quads':False,'flip_v':True,'emissive_ambient':False,
              'mtl_override':'obj_owned_override.mtl',
              'textures':{'surface':'fixture:obj_owned_inherited','override':'fixture:obj_owned_reset'},
              'visibility':{'panel':False,'hidden':False},'render_type':'minecraft:cutout'}
    inherited = {'parent':'fixture:obj_owned_parent','model':'fixture:models/unavailable_child.obj',
                 'automatic_culling':'ignored','shade_quads':0,'flip_v':False,'emissive_ambient':[],
                 'mtl_override':'unavailable_child.mtl',
                 'textures':{'override':'fixture:obj_owned_inherited'},
                 'transform':{'translation':[0.125,0,0]},'visibility':{'panel':True},
                 'render_type':'minecraft:translucent'}
    reset = {'parent':'fixture:obj_owned_parent','loader':'neoforge:obj',
             'model':'fixture:models/obj_owned_reset.obj',
             'textures':{'surface':'fixture:obj_owned_reset'},
             'transform':{'translation':[0,0,0.0625]},'visibility':{'panel':True}}
    for label, model in [('parent',parent),('inherited',inherited),('reset',reset)]:
        put('assets/fixture/models/obj_owned_' + label + '.json', model)
    definition = {'variants':{'mode='+label:{'model':'fixture:obj_owned_'+label}
                              for label in ('inherited','reset')}}
    put('assets/fixture/blockstates/obj_owned.json', definition)
    return parent, inherited, reset, definition
