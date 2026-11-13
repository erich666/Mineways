"""Farmer's Delight 1.3.4 standing/wall canvas bodies, without saved text.

The shipped renderer uses the vanilla spruce sign layer and a fixed entity
texture. Geometry follows SignRenderer.createSignLayer and ModelPart.Cube.
"""
import math
import re
import struct

MODEL = 'farmersdelight:block/canvas_sign'
COLORS = ('', 'white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink',
          'gray', 'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')
BLOCKS = {'farmersdelight:' + (color + '_' if color else '') + suffix: (color, wall)
          for color in COLORS for suffix, wall in (('canvas_sign', False), ('canvas_wall_sign', True))}
FACINGS = {'north': 180, 'east': 270, 'south': 0, 'west': 90}
SCALE = struct.unpack('f', struct.pack('f', 2/3))[0]
MAX_COMPILED_ELEMENTS = 4000000
MAX_VARIANTS = 4096


def matches(block_name, name, model):
    return (block_name in BLOCKS and name == MODEL and 'loader' not in model
            and ('elements' not in model or model['elements'] == []))


def _cube(origin, size, texture_origin, angle, wall, texture):
    x, y, z = origin
    w, h, d = size
    u, v = texture_origin
    points = ((x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z),
              (x,y,z+d),(x+w,y,z+d),(x+w,y+h,z+d),(x,y+h,z+d))
    faces = (((5,4,0,1),(u+d,v,u+d+w,v+d)),
             ((2,3,7,6),(u+d+w,v+d,u+d+2*w,v)),
             ((0,4,7,3),(u,v+d,u+d,v+d+h)),
             ((1,0,3,2),(u+d,v+d,u+d+w,v+d+h)),
             ((5,1,2,6),(u+d+w,v+d,u+2*d+w,v+d+h)),
             ((4,5,6,7),(u+2*d+w,v+d,u+2*d+2*w,v+d+h)))
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    transformed = []
    for px, py, pz in points:
        a, b, q = SCALE*px, -SCALE*py, -SCALE*pz
        if wall:
            b -= 5
            q -= 7
        transformed.append((8+c*a+s*q, 8+b, 8-s*a+c*q))
    output = []
    for corners, (u1,v1,u2,v2) in faces:
        uv = ((u2/4,v1/2),(u1/4,v1/2),(u1/4,v2/2),(u2/4,v2/2))
        output.append({'texture':texture, 'vertices': [(*transformed[i],*t) for i,t in zip(corners,uv)]})
    return output


def _orientation(state, wall, error_type):
    if wall:
        facing = state.get('facing', 'north')
        if facing not in FACINGS:
            raise error_type('invalid canvas wall sign facing')
        return -FACINGS[facing]
    rotation = state.get('rotation', '0')
    if not isinstance(rotation, str) or not re.fullmatch(r'(?:[0-9]|1[0-5])', rotation):
        raise error_type('canvas sign rotation must be 0..15')
    return -22.5*int(rotation)


def compile_canvas_sign(compiler, name, model, rotation, selector, depth, error_type, block_name, decode_png):
    if not matches(block_name, name, model):
        raise error_type('canvas sign requires a verified empty Farmer\'s Delight model')
    color, wall = BLOCKS[block_name]
    state = selector or {}
    key = 'facing' if wall else 'rotation'
    if depth and key not in state:
        raise error_type('nested canvas sign requires explicit palette orientation')
    angle = _orientation(state, wall, error_type)
    count = 6 if wall else 12
    if compiler.element_count + count > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled canvas sign polygon budget exceeded')
    texture = compiler.texture('farmersdelight:entity/signs/canvas' + ('_'+color if color else ''))
    # The renderer uses entityCutoutNoCull, not fractional-alpha blending.
    if any(alpha not in (0,255) for alpha in decode_png(compiler.textures[texture][0])[2][3::4]):
        raise error_type('canvas sign cutout texture requires binary alpha (0 or 255)')
    polygons = _cube((-12,-14,-1),(24,12,2),(0,0),angle,wall,texture)
    if not wall:
        polygons += _cube((-1,-2,-1),(2,14,2),(0,14),angle,wall,texture)
    compiler.element_count += len(polygons)
    compiler.warnings.add(block_name + ': canvas sign body without saved front/back text, text dye or glowing text')
    if any(rotation) or model.get('transform', {}) or model.get('visibility') or model.get('textures'):
        compiler.warnings.add(block_name + ': model texture aliases, rotations, UV lock, visibility and root transforms are ignored by the shipped sign renderer')
    return {'rotation':(0,0,0),'elements':[],'polygons':polygons,'fixed_rotation':True}


def expand_canvas_sign(compiler, name, definition, error_type):
    if name not in BLOCKS:
        return definition
    def applies(application):
        chosen = application[0] if isinstance(application,list) and application else application
        if not isinstance(chosen,dict) or chosen.get('model') != MODEL:
            return False
        return matches(name, chosen['model'], compiler.model(chosen['model']))
    variants = definition.get('variants')
    if not isinstance(variants,dict):
        parts = definition.get('multipart',[])
        if isinstance(parts,list) and any(isinstance(part,dict) and applies(part.get('apply')) for part in parts):
            raise error_type('canvas sign palette orientation requires variants, not multipart')
        return definition
    if len(variants) > MAX_VARIANTS:
        return definition
    wall = BLOCKS[name][1]
    flag = 'facing' if wall else 'rotation'
    values = FACINGS if wall else tuple(str(i) for i in range(16))
    output, changed = dict(variants), False
    for selector, application in variants.items():
        if not applies(application):
            continue
        if not isinstance(selector,str) or len(selector)>1023:
            raise error_type('invalid canvas sign selector')
        state = {}
        for pair in selector.split(',') if selector else ():
            key, sep, value = pair.partition('=')
            if not sep or not re.fullmatch(r'[a-z0-9_]+',key) or not re.fullmatch(r'[a-z0-9_.-]+',value) or key in state:
                raise error_type('invalid canvas sign selector')
            state[key] = value
        if len(state)>64:
            raise error_type('excessive canvas sign selector properties')
        _orientation(state,wall,error_type)
        if flag in state:
            continue
        changed = True
        for value in values:
            expanded = {**state,flag:value}
            key = ','.join(k+'='+expanded[k] for k in sorted(expanded))
            if key in output and output[key] != application:
                raise error_type('overlapping canvas sign state selectors')
            output[key] = application
            if len(output)>MAX_VARIANTS:
                raise error_type('canvas sign expansion exceeds 4096 selectors')
    return {**definition,'variants':output} if changed else definition
