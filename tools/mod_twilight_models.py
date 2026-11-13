"""Static paths verified in Twilight Forest 1.21.1-4.8.3345.

ConnectedTextureModel uses ConnectionLogic.NONE when neighbor ModelData is absent.
Its four quadrants partition a whole cube face with unchanged 0..16 UV mapping.
ForceFieldModel selects supplied conditional elements, but its diagonal connections
and face suppression require a live world. We retain the saved cardinal directions.
"""
import hashlib
import re
import struct

CONNECTED = 'twilightforest:connected_texture_block'
FORCE_FIELD = 'twilightforest:force_field'
LOADERS = (CONNECTED, FORCE_FIELD)
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')
# ExtraDirection only adds orthogonal pairs (no opposite-axis pairs or triples).
EXTRA_DIRECTIONS = frozenset(DIRECTIONS) | frozenset(
    a + '_' + b for a, b in (('down','north'), ('down','south'), ('down','west'), ('down','east'),
                             ('up','north'), ('up','south'), ('up','west'), ('up','east'),
                             ('north','west'), ('north','east'), ('south','west'), ('south','east')))
DOOR_COLORS = {'twilightforest:' + color + '_castle_door': value for color, value in
               (('pink', 0xffff00ff), ('blue', 0xff00ffff), ('yellow', 0xffffff00), ('violet', 0xff4b0082))}
# ColorHandler's null-world aurora hue .45 -> RGB(0,255,178), then rgbToHSV,
# saturation*.5 and value=min(value+.4,.9). ColorUtil rounds each RGB component.
AURORA_COLOR = 0xff73e6c3
RESOURCE = re.compile(r'[a-z0-9_.-]+:[a-z0-9_./-]+\Z')
MAX_GENERATED_PIXELS = 64 * 1024 * 1024


def _layer(value, error_type):
    if not isinstance(value, dict):
        raise error_type('Twilight connected texture layer must be an object')
    tint, emission = value.get('tint_index', -1), value.get('emissivity', 0)
    if type(tint) is not int or not -1 <= tint <= 255 or type(emission) is not int or not 0 <= emission <= 15:
        raise error_type('invalid Twilight tint index or emissivity')
    return tint, emission


def _tinted(compiler, model, key, options, block_name, label, error_type):
    tint, emission = _layer(options, error_type)
    texture = compiler.texture(compiler.loader_texture(model, key))
    if tint >= 0:
        color = DOOR_COLORS.get(block_name)
        if block_name == 'twilightforest:auroralized_glass':
            color = AURORA_COLOR
            compiler.warnings.add(label + ': auroralized glass uses the verified no-world color; position-dependent aurora colors are not evaluated')
        if color is None:
            raise error_type('Twilight connected texture tint has no verified block color: ' + str(block_name))
        texture = compiler.face_color_texture(texture, {}, {'neoforge_data': {'color': f'{color:08x}'}}, label)
    if emission:
        compiler.warnings.add(label + ': Twilight face emissivity is not exported as material emission')
    return texture


def _overlay(compiler, base, overlay, error_type):
    name = 'mineways:twilight/overlay_' + hashlib.sha256((base + '\n' + overlay).encode()).hexdigest()
    if name not in compiler.textures:
        size = max(struct.unpack('>I', compiler.textures[t][0][16:20])[0] for t in (base, overlay))
        if compiler.face_texture_pixel_count + size * size > MAX_GENERATED_PIXELS:
            raise error_type('Twilight generated-texture pixel budget exceeded')
        compiler.overlay_texture(name, base, overlay)
        compiler.face_texture_pixels[name] = size * size
        compiler.face_texture_pixel_count += size * size
    return name


def _has_base_texture(model, error_type):
    # BlockGeometryBakingContext.hasMaterial delegates to BlockModel.hasTexture.
    # Missing entries and cyclic aliases resolve to missingno, hence no base.
    textures = model.get('textures', {})
    key, seen = 'base_texture', set()
    while True:
        if key not in textures or key in seen:
            return False
        if len(seen) >= 64:
            raise error_type('excessive Twilight base texture alias chain')
        seen.add(key)
        value = textures[key]
        if not isinstance(value, str):
            raise error_type('Twilight base texture reference must be a string')
        if not value.startswith('#'):
            return value not in ('minecraft:missingno', 'missingno')
        key = value[1:]


def _connected(compiler, model, block_name, label, error_type):
    if model.get('transform', {}) != {}:
        raise error_type('Twilight connected texture root transforms require an explicit replacement model')
    base_options, options = model.get('base', {}), model.get('connected_texture', {})
    _layer(base_options, error_type)
    _layer(options, error_type)
    faces = options.get('faces', list(DIRECTIONS))
    enabled = options.get('render_disabled_faces', True)
    if (not isinstance(faces, list) or len(faces) > 256 or any(not isinstance(f, str) or f not in DIRECTIONS for f in faces)
            or type(enabled) is not bool):
        raise error_type('invalid Twilight connected texture faces')
    connectable = model.get('connectable_blocks', [])
    if (not isinstance(connectable, list) or len(connectable) > 4096
            or any(not isinstance(v, str) or not RESOURCE.fullmatch(v.removeprefix('#')) for v in connectable)):
        raise error_type('invalid Twilight connectable block resources')
    overlay = _tinted(compiler, model, 'overlay_texture', options, block_name, label, error_type)
    base = (_tinted(compiler, model, 'base_texture', base_options, block_name, label, error_type)
            if _has_base_texture(model, error_type) else None)
    combined = _overlay(compiler, base, overlay, error_type) if base else overlay
    result = {}
    for direction in DIRECTIONS:
        texture = combined if enabled or direction in faces else base
        if texture is not None:
            result[direction] = {'texture': texture, 'uv': [0, 0, 16, 16], 'cullface': direction}
    compiler.warnings.add(label + ': Twilight connected textures use isolated ConnectionLogic.NONE faces; neighbor connections are not evaluated')
    return [{'from': [0, 0, 0], 'to': [16, 16, 16], 'faces': result}] if result else []


def _force_field(model, selector, error_type):
    supplied = model.get('elements')
    if not isinstance(supplied, list) or not supplied or len(supplied) > 2048:
        raise error_type('Twilight force field requires bounded supplied elements')
    state = selector or {}
    if any(state.get(side, 'false') not in ('true', 'false') for side in DIRECTIONS):
        raise error_type('Twilight force field directions must be true or false')
    active = {side for side in DIRECTIONS if state.get(side) == 'true'}
    result = []
    for element in supplied:
        if not isinstance(element, dict):
            raise error_type('invalid Twilight force field element')
        condition = element.get('condition')
        if condition is not None:
            if not isinstance(condition, dict):
                raise error_type('Twilight force field condition must be an object')
            direction, expected, parents = condition.get('direction', 'up'), condition.get('if', True), condition.get('parents')
            if (not isinstance(direction, str) or direction not in EXTRA_DIRECTIONS or type(expected) is not bool
                    or not isinstance(parents, list) or len(parents) > 18
                    or any(not isinstance(p, str) or p not in EXTRA_DIRECTIONS for p in parents)):
                raise error_type('invalid Twilight force field condition')
            if not all(parent in active for parent in parents) or ((direction in active) != expected):
                continue
        # The shipped renderer passes null BlockElementRotation and X0_Y0.
        copied = {key: value for key, value in element.items() if key not in ('condition', 'rotation')}
        # Any valid cull hint puts the face in its geometric side's query.
        # The dynamic renderer ignores the hint's particular direction value.
        if isinstance(copied.get('faces'), dict):
            copied['faces'] = {side: ({**face, 'cullface': side} if face.get('cullface') in DIRECTIONS else dict(face))
                               if isinstance(face, dict) else face for side, face in copied['faces'].items()}
        result.append(copied)
    return result


def compile_twilight(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    label = name if isinstance(name, str) else '<inline>'
    if depth >= 63:
        raise error_type('excessive Twilight model depth')
    force = model.get('loader') == FORCE_FIELD
    if force:
        elements = _force_field(model, selector, error_type)
        compiler.warnings.add(label + ': Twilight force fields preserve saved cardinal connections; diagonal joins and neighbor face suppression are not evaluated')
        if any(rotation) or model.get('transform') or any('rotation' in e for e in model.get('elements', [])):
            compiler.warnings.add(label + ': supplied rotations, UV lock and root transforms are ignored by the shipped Twilight force-field renderer')
    elif model.get('loader') == CONNECTED:
        elements = _connected(compiler, model, block_name, label, error_type)
    else:
        raise error_type('unsupported Twilight model loader')
    if not elements:
        raise error_type('Twilight model has no visible static supplied geometry')
    source = {'textures': model.get('textures', {}), 'elements': elements}
    application = {'model': source}
    if not force:
        application.update(x=rotation[0], y=rotation[1], uvlock=bool(rotation[2]))
        if len(rotation) == 4:
            application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    if force:
        result['fixed_rotation'] = True
    return result
