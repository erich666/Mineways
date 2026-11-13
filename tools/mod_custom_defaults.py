"""Visible defaults verified from Functional Storage 1.5.8 and LBR 1.9.12."""

FRAMED_LOADER = 'functionalstorage:framedblock'
MICROCHIP_LOADER = 'little_big_redstone:microchip'


def compile_framed(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Absent FRAMED_PROPERTY leaves every visible child's original quads intact."""
    if depth >= 63:
        raise error_type('excessive framed model depth')
    children, parts = model.get('children', {}), model.get('parts', {})
    if not isinstance(children, dict) or not isinstance(parts, dict):
        raise error_type('framed children and parts must be objects')
    if not 1 <= len(children) + len(parts) <= 256:
        raise error_type('framed model requires 1..256 children')
    if children.keys() & parts.keys():
        raise error_type('duplicate framed child in children and parts')
    children = {**children, **parts}
    order = model.get('item_render_order', list(children))
    if not isinstance(order, list) or len(order) > 256 or any(not isinstance(key, str) or key not in children for key in order):
        raise error_type('invalid framed item_render_order')
    visibility = model.get('visibility', {})
    if isinstance(visibility, dict) and any(visibility.get(key, True) is False for key in order):
        raise error_type('framed item_render_order references a hidden child')
    # Item passes do not restrict the world children. Each child owns its textures.
    composite = {'loader': 'neoforge:composite', 'children': children}
    for key in ('visibility', 'transform'):
        if key in model:
            composite[key] = model[key]
    x, y, uvlock = rotation[:3]
    application = {'model': composite, 'x': x, 'y': y, 'uvlock': bool(uvlock)}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: supplied framed default geometry only; saved block-entity design materials are not evaluated')
    return result


def _has_material(textures, key, error_type):
    # BlockModel.hasTexture resolves aliases and compares against missingno.
    value, seen = '#' + key, set()
    while isinstance(value, str) and value.startswith('#'):
        if value in seen or len(seen) >= 64:
            raise error_type('cyclic or excessive microchip texture references')
        seen.add(value)
        if value[1:] not in textures:
            return False
        value = textures[value[1:]]
    if not isinstance(value, str):
        raise error_type('microchip texture must be a resource string')
    return value not in ('missingno', 'minecraft:missingno')


def compile_microchip(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """DEFAULT has six false side flags. ElementBuilder bakes X0_Y0 cube faces."""
    if depth >= 63:
        raise error_type('excessive microchip model depth')
    textures = model.get('textures', {})
    if not isinstance(textures, dict):
        raise error_type('microchip textures must be an object')
    faces = {}
    for side in ('down', 'up', 'north', 'south', 'west', 'east'):
        key = 'base_' + side
        faces[side] = {'texture': '#' + (key if _has_material(textures, key, error_type) else 'base')}
    # The Java loader ignores supplied geometry, root transforms and ModelState.
    # Never propagate application turns or cullface into these generated faces.
    adapted = {'textures': textures, 'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': faces}]}
    result = compiler.compile_model({'model': adapted}, depth + 1, selector, block_name)
    result['fixed_rotation'] = True
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: microchip default base only; saved signal-side configuration and signal overlays are not evaluated')
    return result
