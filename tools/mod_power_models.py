"""Verified static defaults for Modern Dynamics 0.9.6 and Mekanism 10.7.19.85.

These adapters read supplied resources. They never load or execute mod classes.
"""
import re

PIPE_LOADER = 'moderndynamics:pipe'
CUBE_LOADER = 'mekanism:energy_cube'
SIDES = ('bottom', 'top', 'front', 'back', 'left', 'right')
CUBE_FIELDS = ('frame',) + tuple(side + suffix for side in SIDES for suffix in ('LEDs', 'Port'))
LOADERS = (PIPE_LOADER, CUBE_LOADER)

# Clockwise from outside in Minecraft's Y-up coordinate system.
CORNERS = {
    'down': ((5,5,11),(5,5,5),(11,5,5),(11,5,11)),
    'up': ((5,11,5),(5,11,11),(11,11,11),(11,11,5)),
    'north': ((11,5,5),(5,5,5),(5,11,5),(11,11,5)),
    'south': ((5,5,11),(11,5,11),(11,11,11),(5,11,11)),
    'west': ((5,5,5),(5,5,11),(5,11,11),(5,11,5)),
    'east': ((11,5,11),(11,5,5),(11,11,5),(11,11,11)),
}
OPPOSITE = dict(zip(CORNERS, ('up','down','south','north','east','west')))


def pipe_uv(side, point):
    """TextureHelper spriteBake flag4 uses the face's position-derived UVs."""
    x, y, z = point
    return {'down': (x,16-z), 'up': (x,z), 'north': (16-x,16-y),
            'south': (x,16-y), 'west': (z,16-y), 'east': (16-z,16-y)}[side]


def compile_power_model(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    if depth >= 63:
        raise error_type('excessive power model depth')
    label = name if isinstance(name, str) else '<inline>'
    if model.get('loader') == PIPE_LOADER:
        kind = model.get('pipeType')
        transparent = model.get('transparent')
        if (not isinstance(kind, str) or not 1 <= len(kind) <= 900
                or not re.fullmatch(r'[a-z0-9_./-]+', kind)
                or any(part in ('', '.', '..') for part in kind.split('/'))
                or type(transparent) is not bool):
            raise error_type('Modern Dynamics pipe requires a bounded pipeType path and boolean transparent')
        texture = compiler.texture('moderndynamics:pipe/' + kind + '/base')
        polygons = []
        for side, points in CORNERS.items():
            polygons.append({'texture': texture,
                             'vertices': [(*p, *pipe_uv(side, p)) for p in points]})
            if transparent:
                # Back faces are real quads, with UVs baked for their opposite
                # nominal direction. They must not depend on billboard options.
                polygons.append({'texture': texture, 'vertices': [
                    (*p, *pipe_uv(OPPOSITE[side], p)) for p in reversed(points)]})
        compiler.warnings.add(f'{label}: Modern Dynamics uses its disconnected block default; live pipe/inventory connections and attachments are unavailable')
        if any(rotation[:3]) or (len(rotation) > 3 and rotation[3]) or model.get('transform', {}):
            compiler.warnings.add(f'{label}: supplied rotations, UV lock and root transforms are ignored by the shipped Modern Dynamics pipe loader')
        return {'rotation': (0,0,0), 'elements': [], 'polygons': polygons, 'fixed_rotation': True}
    if model.get('loader') != CUBE_LOADER:
        raise error_type('unsupported power model loader')
    if model.get('transform', {}) != {}:
        raise error_type('energy cube root transforms require an explicit replacement model')
    total = 0
    for field in CUBE_FIELDS:
        elements = model.get(field)
        if (not isinstance(elements, list) or not elements or len(elements) > 2048
                or any(not isinstance(element, dict) for element in elements)):
            raise error_type('energy cube requires bounded nonempty element arrays: ' + field)
        total += len(elements)
    if total > 2048:
        raise error_type('energy cube source element count exceeds limit')
    # EnergyCubeBakedModel falls back to six INACTIVE states. All unmodified
    # LEDs remain, but no ports or active LED UV/fullbright transforms apply.
    selected = list(model['frame'])
    for side in SIDES:
        selected.extend(model[side + 'LEDs'])
    source = {'textures': model.get('textures', {}), 'elements': selected}
    application = {'model': source, 'x': rotation[0], 'y': rotation[1], 'uvlock': bool(rotation[2])}
    if len(rotation) > 3:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    compiler.warnings.add(f'{label}: energy cube uses inactive sides without ports; saved side configuration and animated stored-energy contents are unavailable')
    return result
