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
