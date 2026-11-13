"""PneumaticCraft 8.2.23 initialized isolated pressure-tube appearance.

PressureTubeBlockEntity initializes both connection masks to zero. The renderer
then selects the supplied disconnected model in six orientations. Absent ModelData
itself emits no quads. We explicitly report this initialized-state approximation.
"""
import copy
import re

LOADER = 'pneumaticcraft:pressure_tube'
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')
# PressureTubeGeometry.ROTATIONS, indexed by Direction.get3DDataValue().
ROTATIONS = ((180, 0), (0, 0), (90, 0), (90, 180), (90, 270), (90, 90))


def _compose(outer, local, orientation):
    """The helper composes application * local, preserving application UV lock."""
    outer_axes = orientation(outer[0], outer[1], outer[3] if len(outer) > 3 else 0)
    local_axes = orientation(*local)
    axes = tuple(tuple(sum(point[j] * outer_axes[j][i] for j in range(3)) for i in range(3))
                 for point in local_axes)
    for z in (0, 90, 180, 270):
        for x in (0, 90, 180, 270):
            for y in (0, 90, 180, 270):
                if orientation(x, y, z) == axes:
                    return (x, y, outer[2], z) if z else (x, y, outer[2])
    raise AssertionError('quarter-turn composition is not a quarter-turn orientation')


def _source(compiler, prefix, error_type):
    path = 'assets/pneumaticcraft/models/block/' + prefix + 'pressure_tube_disconnected.json'
    if path not in compiler.assets.files:
        raise error_type('missing pressure-tube disconnected model: pneumaticcraft:block/' + prefix + 'pressure_tube_disconnected')
    source = compiler.assets.json(path)
    if not isinstance(source, dict):
        raise error_type('pressure-tube disconnected model must be an object')
    # The shipped loader calls BlockModel.fromStream without resolving parents.
    # Do not invent parent geometry or substitute unrelated custom renderers.
    if 'parent' in source or 'loader' in source or source.get('transform', {}) != {}:
        raise error_type('pressure-tube source parents, custom loaders and root transforms require an explicit self-contained replacement model')
    source = copy.deepcopy(source)
    elements = source.get('elements')
    if not isinstance(elements, list) or not elements or len(elements) > 2048:
        raise error_type('pressure-tube source requires bounded nonempty elements')
    filtered = []
    for element in elements:
        if not isinstance(element, dict) or not isinstance(element.get('faces'), dict):
            raise error_type('invalid pressure-tube source element')
        faces = {}
        for side, face in element['faces'].items():
            if side not in DIRECTIONS or not isinstance(face, dict):
                raise error_type('invalid pressure-tube source face')
            cull = face.get('cullface', '-')
            if not isinstance(cull, str):
                raise error_type('invalid pressure-tube source cullface')
            # PressureTubeModel queries each child with side=null only. Culled
            # face lists are never queried, even when there is no opaque neighbor.
            if cull not in DIRECTIONS:
                faces[side] = face
        if faces:
            element['faces'] = faces
            filtered.append(element)
    if not filtered:
        raise error_type('pressure-tube source has no visible unculled geometry')
    source['elements'] = filtered
    return source


def compile_pressure_tube(compiler, name, model, rotation, selector, depth, error_type,
                          block_name, orientation):
    if depth >= 63:
        raise error_type('excessive pressure-tube model depth')
    if model.get('loader') != LOADER:
        raise error_type('unsupported pressure-tube model loader')
    prefix = model.get('prefix', '')
    if (not isinstance(prefix, str) or len(prefix) > 900
            or (prefix and (not re.fullmatch(r'[a-z0-9_./-]+', prefix)
                            or any(part in ('', '.', '..') for part in prefix.split('/'))))):
        raise error_type('pressure-tube prefix must be a bounded resource path')
    # The actual loader appends '_' whenever the key is present, even if empty.
    prefix = prefix + '_' if 'prefix' in model else ''
    source = _source(compiler, prefix, error_type)
    compiled = compiler.compile_model({'model': source}, depth + 1, selector, block_name)
    if (compiled.get('invisible') or compiled.get('polygons') or 'split_models' in compiled
            or 'split_policy' in compiled or not compiled.get('elements')):
        raise error_type('pressure-tube source needs visible standard element geometry')
    parts = [{**compiled, 'rotation': _compose(rotation, local, orientation)} for local in ROTATIONS]
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(label + ': pressure tube uses initialized isolated appearance; saved closed ends, live connections, modules and camouflage are not evaluated')
    if model.get('transform', {}) != {} or model.get('visibility') or model.get('textures'):
        compiler.warnings.add(label + ': outer pressure-tube textures, visibility and root transforms are ignored by the shipped loader; replace the disconnected source model instead')
    return {'rotation': rotation, 'elements': [], 'split_models': parts}
