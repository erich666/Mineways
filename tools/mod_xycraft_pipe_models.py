"""XyCraft Machines 0.7.53's exact cache-zero pipe geometry.

Absent ModelPropKey selects zero: the supplied core plus six supplied caps.
The core keeps application ModelState. Caps use independent opposite-direction
quaternions and fresh SimpleModelState with UV lock disabled.
"""
LOADER = 'xycraft_machines:pipe'
FIELDS = ('straight', 'cap', 'core', 'extract', 'insert', 'short_straight')
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')
# Direction.getOpposite().getRotation(): identity, Rx(pi), Rx(pi/2),
# Rx(pi/2)*Rz(pi), Rx(pi/2)*Rz(-pi/2), Rx(pi/2)*Rz(pi/2).
CAP_ROTATIONS = ((0, 0, 0), (180, 0, 0), (270, 0, 0),
                 (270, 0, 0, 180), (270, 0, 0, 90), (270, 0, 0, 270))


def _part(compiler, source, rotation, selector, depth, error_type, block_name):
    resolved = compiler.model(source)
    # Standard box compilation does not implement arbitrary root transforms.
    # OBJ and composite compilation already validate and apply their transforms.
    if (resolved.get('transform', {}) != {}
            and resolved.get('loader') not in ('neoforge:obj', 'neoforge:composite', 'forge:composite')):
        raise error_type('XyCraft pipe child root transform requires an explicit replacement model')
    application = {'model': source, 'x': rotation[0], 'y': rotation[1], 'uvlock': bool(rotation[2])}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    if 'split_models' in result or 'split_policy' in result:
        raise error_type('nested split XyCraft pipe child requires an explicit replacement model')
    if result.get('invisible') or not (result.get('elements') or result.get('polygons')):
        raise error_type('XyCraft pipe child requires visible supplied geometry')
    # PipeBakedModel.getAllQuads collects null plus all six side lists, then
    # returns everything from its null-side query. Neighbor culling is disabled.
    elements = [{**element, 'faces': [(*face[:-1], '-') for face in element['faces']]}
                for element in result['elements']]
    return {**result, 'elements': elements}


def compile_pipe(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    if depth >= 63:
        raise error_type('excessive XyCraft pipe model depth')
    if model.get('loader') != LOADER:
        raise error_type('unsupported XyCraft pipe loader')
    for key in FIELDS:
        if key == 'straight' and key not in model:
            continue
        if not isinstance(model.get(key), dict):
            raise error_type('XyCraft pipe requires an inline model for ' + key)
        # resolveParents visits every child, including unused connection parts.
        compiler.model(model[key])
    core = _part(compiler, model['core'], rotation, selector, depth, error_type, block_name)
    caps = [_part(compiler, model['cap'], turn, selector, depth, error_type, block_name)
            for turn in CAP_ROTATIONS]
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(label + ': XyCraft cache-zero pipe appearance; live insert, extract and transfer connections are not evaluated')
    if model.get('transform', {}) or model.get('textures') or model.get('visibility'):
        compiler.warnings.add(label + ': outer pipe textures, visibility and root transforms are ignored by the shipped loader; replace its inline child models instead')
    return {'rotation': rotation, 'elements': [], 'split_models': [core, *caps]}
