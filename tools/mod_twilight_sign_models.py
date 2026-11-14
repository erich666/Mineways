"""Twilight Forest 4.8.3345 vanilla standing and hanging sign bodies without saved text."""
import itertools
import re

WOODS = ('twilight_oak', 'canopy', 'mangrove', 'dark', 'time', 'transformation', 'mining', 'sorting')
BLOCKS = {'twilightforest:' + (('twilight' if wood == 'twilight_oak' else wood) + '_wall_sign'
                              if wall else wood + '_sign'): (wood, wall)
          for wood in WOODS for wall in (False, True)}
HANGING_BLOCKS = {'twilightforest:' + wood + ('_wall_hanging_sign' if wall else '_hanging_sign'): (wood, wall)
                  for wood in WOODS for wall in (False, True)}
MAX_COMPILED_ELEMENTS = 4000000
MAX_VARIANTS = 4096


def matches(block_name, model):
    return ((block_name in BLOCKS or block_name in HANGING_BLOCKS) and model.get('_builtin_entity') is True
            and 'loader' not in model and 'elements' not in model)


def compile_sign(compiler, name, model, rotation, selector, depth, error_type, block_name, decode_png, canvas_module):
    if not matches(block_name, model):
        raise error_type('Twilight sign requires a verified unchanged entity model')
    hanging = block_name in HANGING_BLOCKS
    wood, wall = (HANGING_BLOCKS if hanging else BLOCKS)[block_name]
    state = selector or {}
    required = ('facing',) if wall else ('rotation','attached') if hanging else ('rotation',)
    if depth and any(key not in state for key in required):
        raise error_type('nested Twilight sign requires explicit palette orientation')
    angle = canvas_module._orientation(state, wall, error_type)
    attached = canvas_module._attached(state, error_type) if hanging and not wall else False
    count = (20 if wall else 8 if attached else 14) if hanging else (6 if wall else 12)
    if compiler.element_count + count > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled Twilight sign polygon budget exceeded')
    texture = canvas_module.texture(compiler, 'twilightforest:entity/signs/' + ('hanging/' if hanging else '') + wood, error_type, decode_png)
    if hanging:
        polygons = canvas_module._hanging_polygons(angle, wall, attached, texture)
    else:
        # All registered TF wood types use vanilla SignRenderer.createSignLayer.
        polygons = canvas_module._cube((-12,-14,-1),(24,12,2),(0,0),angle,wall,texture)
        if not wall:
            polygons += canvas_module._cube((-1,-2,-1),(2,14,2),(0,14),angle,wall,texture)
    compiler.element_count += len(polygons)
    compiler.warnings.add(block_name + ': Twilight sign body without saved front/back text, text dye or glowing text')
    if any(rotation) or model.get('transform', {}) or model.get('visibility'):
        compiler.warnings.add(block_name + ': model rotations, UV lock, visibility and root transforms are ignored by the shipped sign renderer')
    return {'rotation':(0,0,0),'elements':[],'polygons':polygons,'fixed_rotation':True}


def expand_sign(compiler, name, definition, error_type, canvas_module):
    if name not in BLOCKS and name not in HANGING_BLOCKS:
        return definition
    def applies(application):
        chosen = application[0] if isinstance(application, list) and application else application
        return (isinstance(chosen, dict) and 'model' in chosen
                and matches(name, compiler.model(chosen['model'])))
    variants = definition.get('variants')
    if not isinstance(variants, dict):
        parts = definition.get('multipart', [])
        if isinstance(parts, list) and any(isinstance(part, dict) and applies(part.get('apply')) for part in parts):
            raise error_type('Twilight sign palette orientation requires variants, not multipart')
        return definition
    if len(variants) > MAX_VARIANTS:
        return definition
    hanging = name in HANGING_BLOCKS
    wall = (HANGING_BLOCKS if hanging else BLOCKS)[name][1]
    domains = {'facing': tuple(canvas_module.FACINGS)} if wall else {'rotation': tuple(str(i) for i in range(16))}
    if hanging and not wall:
        domains['attached'] = ('false','true')
    output, changed = dict(variants), False
    for selector, application in variants.items():
        if not applies(application):
            continue
        if not isinstance(selector, str) or len(selector) > 1023:
            raise error_type('invalid Twilight sign selector')
        state = {}
        for pair in selector.split(',') if selector else ():
            key, separator, value = pair.partition('=')
            if (not separator or not re.fullmatch('[a-z0-9_]+', key)
                    or not re.fullmatch('[a-z0-9_.-]+', value) or key in state):
                raise error_type('invalid Twilight sign selector')
            state[key] = value
        if len(state) > 64:
            raise error_type('excessive Twilight sign selector properties')
        canvas_module._orientation(state, wall, error_type)
        if hanging and not wall:
            canvas_module._attached(state, error_type)
        keys = [key for key in domains if key not in state]
        if not keys:
            continue
        changed = True
        for values in itertools.product(*((None,*domains[key]) for key in keys)):
            if all(value is None for value in values):
                continue  # The original selector is already retained, including its key order.
            expanded = {**state, **{key:value for key,value in zip(keys,values) if value is not None}}
            key = ','.join(k+'='+expanded[k] for k in sorted(expanded))
            if key in output and output[key] != application:
                raise error_type('overlapping Twilight sign state selectors')
            output[key] = application
            if len(output) > MAX_VARIANTS:
                raise error_type('Twilight sign expansion exceeds 4096 selectors')
    return {**definition, 'variants':output} if changed else definition
