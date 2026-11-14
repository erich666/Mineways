"""Supplied material-model defaults verified against shipped loaders, without Java."""

DOMUM_LOADER = 'domum_ornamentum:materially_textured'


def compile_domum(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Domum 1.0.236 bakes its separate parent with the supplied ModelState.

    Missing material data gives an empty replacement map. The retexturing builder
    then retains the parent's quads. Outer wrapper textures do not replace them.
    """
    if depth >= 63:
        raise error_type('excessive material model depth')
    parent = model.get('_domum_geometry_parent', model.get('parent'))
    if not isinstance(parent, str):
        raise error_type('Domum material model requires a parent resource')
    resolved = compiler.model(parent)
    if any(owner.get('transform', {}) != {} for owner in (model, resolved)):
        raise error_type('Domum root transforms require an explicit replacement model')
    # Four supplied tiled-paperwall side-off models deliberately contain no
    # elements. Keep this exception within the verified loader and explicit leaf
    # shape, so missing/unknown models are never silently declared invisible.
    if resolved.get('elements') == [] and not any(key in resolved for key in ('parent', 'loader')):
        result = {'rotation': rotation, 'elements': [], 'invisible': True}
    else:
        x, y, uvlock = rotation[:3]
        application = {'model': parent, 'x': x, 'y': y, 'uvlock': bool(uvlock)}
        if len(rotation) == 4:
            application['ae2:z'] = rotation[3]
        result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: supplied default material model only; saved block-entity material replacements are not evaluated')
    return result
