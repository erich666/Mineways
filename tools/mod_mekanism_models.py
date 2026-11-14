"""Mekanism 10.7.19.85's inspected fallback for absent transmitter ModelData.

TransmitterBakedModel wraps only internal OBJ groups named direction + NONE.
VisibleModelConfiguration replaces component visibility with an exact allowlist.
The separate glass OBJ and connected/color/content paths require live ModelData.
"""
LOADER = 'mekanism:transmitter'
DISCONNECTED_GROUPS = frozenset(side + 'NONE' for side in ('down', 'up', 'north', 'south', 'east', 'west'))


def compile_transmitter(compiler, model, rotation, api, obj_module):
    if 'glass' in model and not isinstance(model['glass'], dict):
        raise api.ImportError('Mekanism transmitter glass must be an OBJ model object')
    result = obj_module.compile_obj(compiler, model, rotation, api, visible_groups=DISCONNECTED_GROUPS)
    name = api.resource(model.get('model'))
    compiler.warnings.add(name + ': Mekanism disconnected fallback only; live connections, contents, colors and optional glass are not evaluated')
    return result


DATA_LOADER = 'mekanism:data_based'


def compile_data_based(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Select the supplied no-data model, not the online block's emitting model."""
    import re
    references = {}
    for key in ('no_data', 'emitting'):
        value = model.get(key)
        if not isinstance(value, str) or len(value) > 1023:
            raise error_type('Mekanism data-based model requires a resource for ' + key)
        namespace, separator, path = value.partition(':')
        if not separator:
            namespace, path = 'minecraft', namespace
        if (not re.fullmatch('[a-z0-9_.-]+', namespace) or not re.fullmatch('[a-z0-9_./-]+', path)
                or any(part in ('', '.', '..') for part in path.split('/'))):
            raise error_type('invalid Mekanism data-based resource: ' + value)
        references[key] = namespace + ':' + path
        # Both resources are dependencies of the shipped geometry, even though
        # only no_data contributes to this static export.
        compiler.model(references[key])
    selected = compiler.model(references['no_data'])
    if selected.get('transform', {}) != {}:
        raise error_type('Mekanism data-based child root transforms require an explicit replacement model')
    application = {'model': references['no_data'], 'x': rotation[0], 'y': rotation[1], 'uvlock': bool(rotation[2])}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: supplied Mekanism no-data model only; emitting-redstone ModelData is not evaluated')
    if model.get('transform', {}):
        compiler.warnings.add(f'{label}: outer root transforms are ignored by the shipped data-based loader')
    return result
