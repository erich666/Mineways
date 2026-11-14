"""IE 12.4.2 configurable-side models without saved SIDECONFIG ModelData."""

LOADER = 'immersiveengineering:conf_sides'
TYPES = ('side_top_bottom', 'side_vertical', 'vertical', 'all_same_texture')
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')


def compile_conf_sides(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """ModelConfigurableSides defaults every absolute side to IOSideConfig.NONE.

    ConfigSidesModelBase.bake does not use ModelState or root transforms. Its
    six generated quads match standard cube face winding and default full UVs.
    The supplied block's palette properties do not rotate or configure them.
    """
    kind = model.get('type')
    base = model.get('base_name')
    if kind not in TYPES or not isinstance(base, str) or not base:
        raise error_type('IE configurable sides require a supported type and base_name resource')
    if depth >= 63:
        raise error_type('excessive configurable-side model depth')
    faces = {}
    for direction in DIRECTIONS:
        vertical = direction in ('down', 'up')
        part = (direction if vertical else 'side') if kind == 'side_top_bottom' else (
            'up' if vertical and kind != 'all_same_texture' else 'side')
        suffix = part if kind == 'vertical' and not vertical else part + '_none'
        faces[direction] = {'texture': base + '_' + suffix,
                            'uv': [0, 0, 16, 16], 'cullface': direction}
    source = {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': faces}]}
    result = compiler.compile_model({'model': source}, depth + 1, selector, block_name)
    result['fixed_rotation'] = True
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: IE configurable sides use NONE on all faces; saved sideConfig input/output settings are not evaluated')
    if any(rotation[i] for i in (0, 1)) or (len(rotation) > 3 and rotation[3]) or rotation[2] or model.get('transform', {}):
        compiler.warnings.add(f'{label}: supplied rotations, UV lock and root transforms are ignored by the shipped IE configurable-side loader')
    return result
