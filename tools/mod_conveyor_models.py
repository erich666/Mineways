"""IE12.4.2-194's inactive basic/dropper/vertical default conveyor meshes.

The 35 source quads retain exact positions, UVs, and winding from
ModelConveyor.getBaseConveyor and ModelUtils.createBakedQuad. No mod code runs.
"""
import re

LOADER = 'immersiveengineering:models/conveyor'
FIELDS = ('conveyorType',)
FACINGS = ('north', 'east', 'south', 'west')
TYPES = ('immersiveengineering:basic', 'immersiveengineering:dropper', 'immersiveengineering:vertical')
MAX_COMPILED_ELEMENTS = 4000000
# Pixel-coordinate rings, ordered NW,SW,SE,NE, from the source's unit rings.
RINGS = {}
for name, x0, x1, y, z0, z1 in (
    ('bottom',0,16,0,0,16), ('bottomBelt',1,15,0,0,16),
    ('top',0,16,2,0,16), ('topBelt',1,15,2,0,16),
    ('corner',0,16,3,0,16), ('cornerBelt',1,15,3,0,16),
    ('wallOuter',0,16,2,1,15), ('wallInner',1,15,2,1,15),
    ('wallOuterTop',0,16,3,1,15), ('wallInnerTop',1,15,3,1,15)):
    RINGS[name] = ((x0,y,z0),(x0,y,z1),(x1,y,z1),(x1,y,z0))

# Material, source corners, UV rectangle, source invert flag. Both corner pairs
# and both walls are present in the inspected absent-instance default.
QUADS = (
    ('belt', (('bottomBelt', 0), ('bottomBelt', 1), ('bottomBelt', 2), ('bottomBelt', 3)), (1, 0, 15, 16), True),
    ('casing_walls', (('bottom', 0), ('bottom', 1), ('bottom', 2), ('bottom', 3)), (0, 0, 16, 16), True),
    ('belt', (('top', 0), ('top', 1), ('top', 2), ('top', 3)), (0, 16, 16, 0), False),
    ('casing_side', (('bottom', 0), ('bottom', 1), ('top', 1), ('top', 0)), (0, 0, 2, 16), False),
    ('casing_side', (('bottom', 2), ('bottom', 3), ('top', 3), ('top', 2)), (0, 0, 2, 16), False),
    ('belt', (('bottomBelt', 0), ('topBelt', 0), ('topBelt', 3), ('bottomBelt', 3)), (1, 2, 15, 0), False),
    ('casing_walls', (('bottom', 0), ('top', 0), ('top', 3), ('bottom', 3)), (0, 2, 16, 0), False),
    ('belt', (('bottomBelt', 1), ('topBelt', 1), ('topBelt', 2), ('bottomBelt', 2)), (1, 0, 15, 2), True),
    ('casing_walls', (('bottom', 1), ('top', 1), ('top', 2), ('bottom', 2)), (0, 0, 16, 2), True),
    ('casing_walls', (('wallOuterTop', 1), ('corner', 1), ('cornerBelt', 1), ('wallInnerTop', 1)), (0, 1, 1, 0), False),
    ('casing_walls', (('wallOuterTop', 2), ('corner', 2), ('cornerBelt', 2), ('wallInnerTop', 2)), (15, 1, 16, 0), True),
    ('casing_walls', (('top', 1), ('corner', 1), ('cornerBelt', 1), ('topBelt', 1)), (0, 2, 1, 3), True),
    ('casing_walls', (('top', 2), ('corner', 2), ('cornerBelt', 2), ('topBelt', 2)), (15, 2, 16, 3), False),
    ('casing_walls', (('wallOuter', 1), ('wallOuterTop', 1), ('wallInnerTop', 1), ('wallInner', 1)), (0, 2, 1, 3), False),
    ('casing_walls', (('wallOuter', 2), ('wallOuterTop', 2), ('wallInnerTop', 2), ('wallInner', 2)), (15, 2, 16, 3), True),
    ('casing_side', (('wallOuter', 1), ('top', 1), ('corner', 1), ('wallOuterTop', 1)), (0, 2, 1, 3), False),
    ('casing_side', (('wallInner', 1), ('topBelt', 1), ('cornerBelt', 1), ('wallInnerTop', 1)), (15, 2, 16, 3), True),
    ('casing_side', (('top', 2), ('wallOuter', 2), ('wallOuterTop', 2), ('corner', 2)), (15, 2, 16, 3), False),
    ('casing_side', (('topBelt', 2), ('wallInner', 2), ('wallInnerTop', 2), ('cornerBelt', 2)), (0, 2, 1, 3), True),
    ('casing_walls', (('wallOuterTop', 3), ('corner', 3), ('cornerBelt', 3), ('wallInnerTop', 3)), (0, 16, 1, 15), False),
    ('casing_walls', (('wallOuterTop', 0), ('corner', 0), ('cornerBelt', 0), ('wallInnerTop', 0)), (15, 16, 16, 15), True),
    ('casing_walls', (('top', 3), ('corner', 3), ('cornerBelt', 3), ('topBelt', 3)), (0, 2, 1, 3), True),
    ('casing_walls', (('top', 0), ('corner', 0), ('cornerBelt', 0), ('topBelt', 0)), (15, 2, 16, 3), False),
    ('casing_walls', (('wallOuter', 3), ('wallOuterTop', 3), ('wallInnerTop', 3), ('wallInner', 3)), (0, 2, 1, 3), False),
    ('casing_walls', (('wallOuter', 0), ('wallOuterTop', 0), ('wallInnerTop', 0), ('wallInner', 0)), (15, 2, 16, 3), True),
    ('casing_side', (('wallOuter', 3), ('top', 3), ('corner', 3), ('wallOuterTop', 3)), (0, 2, 1, 3), False),
    ('casing_side', (('wallInner', 3), ('topBelt', 3), ('cornerBelt', 3), ('wallInnerTop', 3)), (15, 2, 16, 3), True),
    ('casing_side', (('top', 0), ('wallOuter', 0), ('wallOuterTop', 0), ('corner', 0)), (15, 2, 16, 3), False),
    ('casing_side', (('topBelt', 0), ('wallInner', 0), ('wallInnerTop', 0), ('cornerBelt', 0)), (0, 2, 1, 3), True),
    ('casing_walls', (('wallOuterTop', 0), ('wallOuterTop', 1), ('wallInnerTop', 1), ('wallInnerTop', 0)), (0, 15, 1, 1), False),
    ('casing_side', (('wallOuter', 0), ('wallOuter', 1), ('wallOuterTop', 1), ('wallOuterTop', 0)), (2, 15, 3, 1), False),
    ('casing_side', (('wallInner', 0), ('wallInner', 1), ('wallInnerTop', 1), ('wallInnerTop', 0)), (2, 15, 3, 1), True),
    ('casing_walls', (('wallOuterTop', 2), ('wallOuterTop', 3), ('wallInnerTop', 3), ('wallInnerTop', 2)), (15, 15, 16, 1), False),
    ('casing_side', (('wallInner', 3), ('wallInner', 2), ('wallInnerTop', 2), ('wallInnerTop', 3)), (2, 15, 3, 1), False),
    ('casing_side', (('wallOuter', 3), ('wallOuter', 2), ('wallOuterTop', 2), ('wallOuterTop', 3)), (2, 15, 3, 1), True),
)


def _orient(point, facing, vertical):
    x, y, z = point
    if vertical:
        x, y, z = x, 16-z, y  # Base yaw composed with T(0,1,0)*Rx(+90).
    return {'north': (x,y,z), 'east': (16-z,y,x),
            'south': (16-x,y,16-z), 'west': (z,y,16-x)}[facing]


def compile_conveyor(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    kind = model.get('conveyorType')
    if model.get('loader') != LOADER or not isinstance(kind, str) or kind not in TYPES:
        raise error_type('unsupported IE conveyor type; requires basic, dropper or vertical')
    state = selector or {}
    if depth and 'facing' not in state:
        raise error_type('nested conveyor requires explicit facing palette property')
    facing = state.get('facing', 'north')
    if facing not in FACINGS:
        raise error_type('invalid conveyor facing')
    if compiler.element_count + len(QUADS) > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled conveyor polygon budget exceeded')
    prefix = 'immersiveengineering:block/conveyor/'
    belt = {'immersiveengineering:basic': 'off', 'immersiveengineering:dropper': 'dropper_off',
            'immersiveengineering:vertical': 'vertical_off'}[kind]
    textures = {role: compiler.texture(prefix + (belt if role == 'belt' else role))
                for role in ('belt', 'casing_side', 'casing_walls')}
    polygons = []
    for role, corners, (u0,v0,u1,v1), invert in QUADS:
        points = [_orient(RINGS[key][index], facing, kind.endswith(':vertical')) for key, index in corners]
        if invert:
            points.reverse()
            uv = ((u1,v0),(u1,v1),(u0,v1),(u0,v0))
        else:
            uv = ((u0,v0),(u0,v1),(u1,v1),(u1,v0))
        polygons.append({'texture': textures[role], 'vertices': [(*p,*t) for p,t in zip(points,uv)]})
    compiler.element_count += len(polygons)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(label + ': inactive uncovered conveyor with default walls and palette facing; saved slopes, covers, dyes, activity and live neighbor connections are not evaluated')
    compiler.warnings.add(label + ': per-state facing replaces the shipped order-dependent missing-instance model cache')
    if any(rotation) or model.get('transform', {}) or model.get('textures') or model.get('visibility'):
        compiler.warnings.add(label + ': wrapper textures, visibility, rotations, UV lock and root transforms are ignored by the shipped conveyor renderer')
    return {'rotation': (0,0,0), 'elements': [], 'polygons': polygons, 'fixed_rotation': True}


def expand_conveyor(compiler, name, definition, error_type):
    def matches(application):
        chosen = application[0] if isinstance(application, list) and application else application
        if not isinstance(chosen, dict) or 'model' not in chosen:
            return False
        try:
            model = compiler.model(chosen['model'])
            return model.get('loader') == LOADER and model.get('conveyorType') in TYPES
        except error_type:
            return False
    variants = definition.get('variants')
    if not isinstance(variants, dict):
        parts = definition.get('multipart', [])
        if isinstance(parts, list) and any(isinstance(part, dict) and matches(part.get('apply')) for part in parts):
            raise error_type('conveyor palette facing requires variants, not multipart applications')
        return definition
    if len(variants) > 4096:
        return definition
    output, changed = dict(variants), False
    for selector, application in variants.items():
        if not matches(application):
            continue
        if not isinstance(selector, str) or len(selector) > 1023:
            raise error_type('invalid conveyor selector')
        state = {}
        for pair in selector.split(',') if selector else ():
            key, sep, value = pair.partition('=')
            if not sep or not re.fullmatch('[a-z0-9_]+',key) or not re.fullmatch('[a-z0-9_]+',value) or key in state:
                raise error_type('invalid conveyor selector')
            state[key] = value
        if len(state) > 64 or state.get('facing','north') not in FACINGS:
            raise error_type('invalid conveyor facing or property count')
        if 'facing' in state:
            continue
        changed = True
        for facing in FACINGS:
            values = {**state, 'facing':facing}
            key = ','.join(k+'='+values[k] for k in sorted(values))
            if key in output and output[key] != application:
                raise error_type('overlapping conveyor state selectors')
            output[key] = application
            if len(output) > 4096:
                raise error_type('conveyor expansion exceeds4096 selectors')
    return {**definition,'variants':output} if changed else definition
