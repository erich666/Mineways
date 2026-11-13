"""Static IE 12.4.2 OBJ callbacks, reconstructed from shipped JVM bytecode.

No mod code executes. Block-entity-only choices use each callback's verified
getDefaultKey. Palette selectors replace that default only where extractKey
reads the corresponding block-state property. See docs/mod-support.md limits.
"""
from types import SimpleNamespace

LOADER = 'immersiveengineering:ie_obj'
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')


def expand_block(compiler, name, definition, error):
    """The lantern JSON omits facing because its Java callback consumes it."""
    variants = definition.get('variants')
    if not isinstance(variants, dict) or set(variants) != {''}:
        return definition
    application = variants['']
    if not isinstance(application, dict) or 'model' not in application:
        return definition
    try:
        model = compiler.model(application['model'])
    except error:
        return definition
    if model.get('loader') != LOADER or model.get('callbacks') != 'immersiveengineering:lantern':
        return definition
    # Retain the verified north default for palettes that omit default properties.
    return {**definition, 'variants': {'': application, **{'facing=' + side: application for side in DIRECTIONS}}}


def compile_ie_obj(compiler, model, rotation, selector, api, obj_module):
    error = api.ImportError
    callback = model.get('callbacks', 'immersiveengineering:default')
    known = {'default', 'chute', 'turret', 'logic_unit', 'pipe', 'lantern', 'siren',
             'breaker', 'structural_connector', 'cagelamp', 'balloon', 'connector_rs',
             'probe', 'razor_wire', 'floodlight', 'submodel', 'structural_arm',
             'post', 'cloche', 'bottling_machine', 'chunk_loader', 'shelf', 'workbench'}
    if not isinstance(callback, str) or callback not in {'immersiveengineering:' + key for key in known}:
        raise error('unsupported Immersive Engineering OBJ callback: ' + str(callback))
    callback = callback.split(':')[1]
    if 'dynamic' in model and type(model['dynamic']) is not bool:
        raise error('IE OBJ dynamic must be boolean')
    if model.get('transform') or model.get('visibility'):
        raise error('IE OBJ root transforms/visibility are unsupported')
    layers = model.get('layers', ['solid'])
    if not isinstance(layers, list) or not layers or len(layers) > 16 or any(not isinstance(v, str) for v in layers):
        raise error('invalid IE OBJ render layers')
    layers = {layer.removeprefix('minecraft:') for layer in layers}
    if layers - {'solid', 'cutout', 'cutout_mipped', 'translucent', 'tripwire'}:
        raise error('unsupported IE OBJ render layer')
    props = selector or {}
    facing = props.get('facing')
    if facing is not None and facing not in DIRECTIONS:
        raise error('invalid IE OBJ facing selector')
    name = api.resource(model.get('model'))
    defaults = {
        'chute': 'callback default vertical base only; diagonal mode and neighbor-dependent chute walls are not available from the block palette',
        'turret': 'static turret base only; the block-entity gun/action renderer is not exported',
        'logic_unit': 'callback default with no installed circuit boards; inventory-dependent geometry is not exported',
        'pipe': 'callback default disconnected center with no cover or dye; neighbor connections and block-entity configuration are not exported',
        'breaker': 'callback default zero extra rotation; block-entity switch rotation is not available from the block palette',
        'structural_connector': 'callback default zero connector rotation; block-entity rotation is not available from the block palette',
        'balloon': 'callback default style 0 with black/purple dye; block-entity colors, style and shader skins are not exported',
        'connector_rs': 'callback default input mode with white channel dye; block-entity mode/channel are not available from the block palette',
        'probe': 'callback default white receiving/sending channel dyes; block-entity channels are not available from the block palette',
        'razor_wire': 'callback default unstacked airborne wire; neighbor-dependent wooden supports are not exported',
        'floodlight': 'callback default north beam direction and zero beam angles with palette mounting side; block-entity beam rotation is not exported',
        'post': 'callback default post base without connection arms; neighbor-dependent geometry is not exported',
        'cloche': 'callback default empty soil; block-entity soil, crop and inventory appearance are not exported',
        'bottling_machine': 'static bottling-machine groups; animated contents are not exported',
        'chunk_loader': 'callback default inactive model without paper; block-entity activity is not available from the block palette',
        'shelf': 'callback default empty shelf; stored crate geometry is not exported',
        'workbench': 'callback default cube0 without blueprint; inventory-dependent geometry is not exported',
        'structural_arm': 'callback default one-block north-facing floor slope; block-entity length, position, ceiling attachment and facing are not exported',
        'submodel': 'callback default shows all source groups; external animated submodel selection is not exported',
    }
    if callback in defaults:
        compiler.warnings.add(name + ': ' + defaults[callback])
    compiler.warnings.add(name + ': IE static OBJ geometry; shader skins, live animation and emitted-light behavior are not exported')

    def visible(group):
        if callback in ('chute', 'turret', 'post'):
            return group == 'base'
        if callback == 'workbench':
            return group == 'cube0'
        if callback == 'shelf':
            return not group.startswith('crate_')
        if callback == 'chunk_loader':
            if group == 'paper':
                return False
            return ('translucent' if group == 'glass' else 'cutout' if group == 'amethyst' else 'solid') in layers
        if callback in ('cloche', 'bottling_machine'):
            return 'translucent' in layers if group == 'glass' else bool(layers - {'translucent'})
        if callback == 'logic_unit':
            return group == 'tubes' and 'translucent' in layers or group == 'base' and bool(layers - {'translucent'})
        if callback == 'pipe':
            return group == 'center'
        if callback == 'lantern':
            attachment = dict(zip(DIRECTIONS, ('attach_t', 'attach_b', 'attach_n', 'attach_s', 'attach_w', 'attach_e')))
            return group in ('base', attachment[facing or 'north'])
        if callback == 'siren':
            return group in (('main', 'base', 'extra_horn') if facing in (None, 'down', 'up') else ('main', 'wallmount'))
        if callback == 'connector_rs':
            return group != 'io_out'
        if callback in ('probe', 'floodlight'):
            return ('translucent' if group == 'glass' else 'cutout' if callback == 'probe' else 'solid') in layers
        if callback == 'razor_wire':
            return bool(group) and not group.startswith('wood')
        return True

    def color(group, material):
        # Color4.from(DyeColor) uses 1.21.1 textureDiffuseColor, not map/firework colors.
        if callback == 'balloon':
            if group.startswith('balloon1_'):
                return 0xff8932b8
            if group.startswith('balloon0_'):
                return 0xff1d1d21
        if callback == 'connector_rs' and group == 'coloured' or callback == 'probe' and group in ('colour_in', 'colour_out'):
            return 0xfff9fffe
        return 0xffffffff

    def position(group, point):
        return point

    if callback == 'floodlight':
        # applyTransformations replaces (not composes) ModelState rotation.
        # Default key: facing=NORTH, side=DOWN, rotX=rotY=0. Palette supplies side.
        side = facing or 'down'
        xyz = {'down': (0, 0, 180), 'up': (0, 0, 0),
               'north': (90, 180, 0), 'south': (90, 0, 0),
               'west': (90, -90, 90), 'east': (90, 90, -90)}[side]
        delta = {'down': (0, -2, 0), 'up': (0, 2, 0), 'north': (0, 0, -2),
                 'south': (0, 0, 2), 'west': (-2, 0, 0), 'east': (2, 0, 0)}[side]
        # degreeToQuaterion = Ry * Rz * Rx. The source mesh is centered at zero.
        def position(group, point):
            point = list(point)
            for axis, degrees in ((0, xyz[0]), (2, xyz[2]), (1, xyz[1])):
                a, b = ((1, 2), (2, 0), (0, 1))[axis]
                for _ in range((degrees % 360) // 90):
                    point[a], point[b] = -point[b], point[a]
            return tuple(point[i] + 8 + delta[i] for i in range(3))
        rotation = (0, 0, 0)
    adapter = SimpleNamespace(visible=visible, color=color, position=position)
    # IEOBJLoader reads model/callbacks/dynamic/layers, not NeoForge's OBJ
    # parser options. In particular, an ignored mtl_override must not replace
    # the OBJ's own material library. IE always flips V in its quad builder.
    obj_model = {key: value for key, value in model.items() if key not in (
        'automatic_culling', 'shade_quads', 'flip_v', 'emissive_ambient', 'mtl_override')}
    obj_model['flip_v'] = True
    result = obj_module.compile_obj(compiler, obj_model, rotation, api, ie=adapter)
    if callback == 'structural_arm':
        textures = {polygon['texture'] for polygon in result['polygons']}
        if len(textures) != 1:
            raise error('IE structural arm requires one unambiguous source OBJ texture')
        polygons = structural_arm_default(next(iter(textures)))
        compiler.element_count += len(polygons) - len(result['polygons'])
        if compiler.element_count > api.MAX_COMPILED_ELEMENTS:
            raise error('compiled polygon budget exceeded')
        result = {'rotation': (0, 0, 0), 'elements': [], 'polygons': polygons}
    return result


def structural_arm_default(texture):
    """Exact position=0,length=1,onCeiling=false,NORTH generated quad path.

    The shipped callback discards the OBJ geometry. addSides(true) mutates its
    shared vertex array, so the end cap already has one SHRINK application.
    """
    vertices = [(0, 16, 0), (0, 0, 16), (16, 0, 16), (16, 16, 0),
                (0, 0, 0), (0, 0, 16), (16, 0, 16), (16, 0, 0)]
    polygons = []

    def shrink(point):
        return tuple(8 + (v - 8) * .999 for v in point)

    def emit(points, uvs):
        corners = [(*point, *uv) for point, uv in zip(points, uvs)]
        # At length1 the two side quads contain repeated vertices. Preserve
        # their exact triangular surface and UVs, without a zero-area edge.
        corners = [corner for i, corner in enumerate(corners) if corner != corners[i - 1]]
        polygons.append({'texture': texture, 'vertices': corners})

    def culled(indices):
        points = [vertices[i] for i in indices]
        emit(points, [(0, 0), (0, 16), (16, 16), (16, 0)])
        emit([shrink(point) for point in reversed(points)], [(16, 0), (16, 16), (0, 16), (0, 0)])

    culled((0, 1, 2, 3))
    culled((7, 6, 5, 4))
    for inward in (False, True):
        if inward:
            vertices = [shrink(point) for point in vertices]
        for indices, left, right in (((5, 1, 0, 4), 0, 16), ((7, 3, 2, 6), 16, 0)):
            points = [vertices[i] for i in indices]
            if inward:
                points.reverse()
                left, right = right, left
            us = (16, 16, 0, 0) if inward else (0, 0, 16, 16)
            emit(points, list(zip(us, (0, left, right, 0))))
    culled((0, 3, 7, 4))
    return polygons
