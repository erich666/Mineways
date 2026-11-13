"""Verified MoreRed 6.0.0.3 static multipart pieces and MineColonies 1.1.1403 empty rack.

WirePartModel.getQuads returns no quads without its neighbor-derived shape data.
TintRotatingModelBuilder only changes tint indices on ordinary element quads.
RackType.NO_RENDER is serialized as blockrackair and uses an empty JSON model.
"""
WIRE_LOADER = 'morered:wire_parts'
TINT_LOADER = 'morered:rotate_tints'
LOADERS = (WIRE_LOADER, TINT_LOADER)


def compile_wire_model(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    label = name if isinstance(name, str) else '<inline>'
    if model.get('loader') == WIRE_LOADER:
        if any(not isinstance(model.get(key), dict) for key in ('line', 'edge')):
            raise error_type('MoreRed wire parts require inline line and edge models')
        compiler.warnings.add(label + ': only palette-selected static wire nodes and elbows are exported; neighbor-dependent line and edge connections are unavailable')
        return {'rotation': rotation, 'elements': [], 'invisible': True, 'requires_visible_companion': True}
    if model.get('loader') != TINT_LOADER:
        raise error_type('unsupported MoreRed wire model loader')
    if model.get('transform', {}) != {}:
        raise error_type('MoreRed tint model root transforms require an explicit replacement model')
    # Compiler.model has already resolved the standard parent elements/textures.
    # Do not reintroduce an inherited custom loader when compiling the result.
    standard = {key: value for key, value in model.items() if key not in ('loader', 'parent')}
    x, y, uvlock = rotation[:3]
    application = {'model': standard, 'x': x, 'y': y, 'uvlock': bool(uvlock)}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    compiler.warnings.add(label + ': ordinary MoreRed elements preserved; direction-dependent wire power tint is not evaluated')
    return result


def is_empty_rack_model(name, model, selector, block_name):
    return (block_name == 'minecolonies:blockminecoloniesrack'
            and name == 'minecolonies:block/blockairrack'
            and (selector or {}).get('variant') == 'blockrackair'
            and model.get('loader') is None
            and ('elements' not in model or model['elements'] == []))
