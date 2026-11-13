"""Closed Sophisticated Storage 1.6.6.2181 chest meshes and acacia materials.

ChestRenderer builds separate single/left/right ModelPart cubes. Its palette
facing replaces model JSON transforms. StorageTextureManager owns the separate
material-definition inheritance, including per-material child overrides.
"""
import hashlib
import itertools
import re

LOADER = 'sophisticatedstorage:chest'
FACINGS = ('north', 'east', 'south', 'west')
TYPES = ('single', 'left', 'right')
TIERS = {'sophisticatedstorage:' + (tier + '_' if tier != 'wood' else '') + 'chest': tier
         for tier in ('wood', 'copper', 'iron', 'gold', 'diamond', 'netherite')}
TWILIGHT_CHESTS = {'twilightforest:' + wood + suffix: (material, trapped)
                   for wood, material in (('twilight_oak', 'twilight'), ('canopy', 'canopy'),
                                          ('mangrove', 'mangrove'), ('dark', 'darkwood'),
                                          ('time', 'time'), ('transformation', 'transformation'),
                                          ('mining', 'mining'), ('sorting', 'sorting'))
                   for suffix, trapped in (('_chest', False), ('_trapped_chest', True))}
MAX_VARIANTS = 4096
MAX_GENERATED_PIXELS = 64 * 1024 * 1024
MAX_COMPILED_ELEMENTS = 4000000


def _resource(value, error_type):
    if not isinstance(value, str) or len(value) > 1023:
        raise error_type('invalid chest material resource')
    namespace, separator, path = value.partition(':')
    if not separator:
        namespace, path = 'minecraft', namespace
    if (not re.fullmatch('[a-z0-9_.-]+', namespace) or not re.fullmatch('[a-z0-9_./-]+', path)
            or any(part in ('', '.', '..') for part in path.split('/'))):
        raise error_type('invalid chest material resource: ' + value)
    return namespace + ':' + path


def _materials(compiler, error_type):
    cached = getattr(compiler, '_closed_chest_materials', None)
    if cached is not None:
        return cached
    # The manager indexes wood by resource path, ignoring the namespace. More
    # than one acacia definition would depend on its map iteration order.
    matches = [path for path in compiler.assets.files
               if re.fullmatch(r'assets/[^/]+/storage_texture_definitions/acacia_chest\.json', path)]
    if len(matches) != 1:
        raise error_type('chest renderer requires one unambiguous acacia material definition')

    def read(reference, chain=()):
        reference = _resource(reference, error_type)
        if reference in chain or len(chain) >= 64:
            raise error_type('cyclic or excessive chest material inheritance')
        namespace, name = reference.split(':', 1)
        source = compiler.assets.json(f'assets/{namespace}/storage_texture_definitions/{name}.json')
        if not isinstance(source, dict):
            raise error_type('chest material definition must be an object')
        kind, textures = '', {}
        if 'parent' in source:
            kind, parent = read(source['parent'], (*chain, reference))
            textures = {key: dict(value) for key, value in parent.items()}
        kind = source.get('type', kind)
        if kind != 'chest':
            raise error_type('chest material definition requires type chest')
        own = source.get('textures', {})
        if not isinstance(own, dict) or len(own) > 32:
            raise error_type('invalid or excessive chest material groups')
        for group, values in own.items():
            if not isinstance(group, str) or len(group) > 64 or not isinstance(values, dict) or len(values) > 32:
                raise error_type('invalid or excessive chest material entries')
            target = textures.setdefault(group, {})
            for key, value in values.items():
                if not isinstance(key, str) or len(key) > 64:
                    raise error_type('invalid chest material key')
                target[key] = _resource(value, error_type)
        if len(textures) > 32 or any(len(values) > 32 for values in textures.values()):
            raise error_type('excessive inherited chest materials')
        return kind, textures
    namespace = matches[0].split('/')[1]
    _, materials = read(namespace + ':acacia_chest')
    compiler._closed_chest_materials = materials
    return materials


def _textures(compiler, chest_type, tier, error_type, decode_png):
    materials = _materials(compiler, error_type).get(chest_type, {})
    if 'base' not in materials or tier + '_tier' not in materials:
        raise error_type('missing chest base or tier material for ' + chest_type)
    base = compiler.texture(materials['base'])
    overlay = compiler.texture(materials[tier + '_tier'])
    name = 'mineways:chest/closed_' + hashlib.sha256((base + '\n' + overlay).encode()).hexdigest()
    if name not in compiler.textures:
        size = 0
        for texture in (base, overlay):
            width, _, pixels = decode_png(compiler.textures[texture][0])
            if any(alpha not in (0, 255) for alpha in pixels[3::4]):
                raise error_type('chest cutout textures require binary alpha (0 or 255): ' + texture)
            size = max(size, width)
        if compiler.face_texture_pixel_count + size * size > MAX_GENERATED_PIXELS:
            raise error_type('chest generated-texture pixel budget exceeded')
        compiler.overlay_texture(name, base, overlay)
        compiler.face_texture_pixels[name] = size * size
        compiler.face_texture_pixel_count += size * size
    return name, overlay


def _orient(point, facing):
    x, y, z = point
    return {'south': (x, y, z), 'west': (16-z, y, x),
            'north': (16-x, y, 16-z), 'east': (z, y, 16-x)}[facing]


def _cube(origin, size, uv_origin, facing, texture):
    x, y, z = origin
    w, h, d = size
    u, v = uv_origin
    points = [(x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z),
              (x,y,z+d),(x+w,y,z+d),(x+w,y+h,z+d),(x,y+h,z+d)]
    faces = (((5,4,0,1),(u+d,v,u+d+w,v+d)),
             ((2,3,7,6),(u+d+w,v+d,u+d+2*w,v)),
             ((0,4,7,3),(u,v+d,u+d,v+d+h)),
             ((1,0,3,2),(u+d,v+d,u+d+w,v+d+h)),
             ((5,1,2,6),(u+d+w,v+d,u+2*d+w,v+d+h)),
             ((4,5,6,7),(u+2*d+w,v+d,u+2*d+2*w,v+d+h)))
    output = []
    for corners, (u1,v1,u2,v2) in faces:
        coords = ((u2/4,v1/4),(u1/4,v1/4),(u1/4,v2/4),(u2/4,v2/4))
        output.append({'texture': texture,
                       'vertices': [(*_orient(points[index], facing), *uv) for index, uv in zip(corners, coords)]})
    return output


def _closed_geometry(chest_type, facing, body, latch):
    x, width, latch_x, latch_width = {'single': (1,14,7,2), 'right': (1,15,15,1), 'left': (0,15,0,1)}[chest_type]
    return (_cube((x,0,1),(width,10,14),(0,19),facing,body)
                + _cube((x,9,1),(width,5,14),(0,0),facing,body)
                + _cube((latch_x,7,15),(latch_width,4,1),(0,0),facing,latch))


def compile_chest(compiler, name, model, rotation, selector, depth, error_type, block_name, decode_png):
    if model.get('loader') != LOADER or block_name not in TIERS:
        raise error_type('chest renderer requires a verified storage block identity')
    state = selector or {}
    if depth and any(key not in state for key in ('facing', 'type')):
        raise error_type('nested chest requires explicit facing and type palette properties')
    facing, chest_type = state.get('facing', 'north'), state.get('type', 'single')
    if facing not in FACINGS or chest_type not in TYPES:
        raise error_type('invalid chest facing or type')
    if compiler.element_count + 18 > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled chest polygon budget exceeded')
    body, latch = _textures(compiler, chest_type, TIERS[block_name], error_type, decode_png)
    polygons = _closed_geometry(chest_type, facing, body, latch)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: closed undyed acacia chest with visible tier and latch; saved wood, dyes, tier hiding, opening animation, packing, displayed items, lock indicators and upgrades are not evaluated')
    if any(rotation) or model.get('transform', {}):
        compiler.warnings.add(f'{label}: supplied rotations, UV lock and root transforms are ignored by the shipped chest block-entity renderer')
    compiler.element_count += len(polygons)
    return {'rotation': (0,0,0), 'elements': [], 'polygons': polygons, 'fixed_rotation': True}


def expand_chest(compiler, name, definition, error_type):
    def matches(application):
        chosen = application[0] if isinstance(application, list) and application else application
        if not isinstance(chosen, dict) or 'model' not in chosen:
            return False
        try:
            resolved = compiler.model(chosen['model'])
            return resolved.get('loader') == LOADER or is_twilight_chest(name, resolved)
        except error_type:
            return False
    variants = definition.get('variants')
    if not isinstance(variants, dict):
        parts = definition.get('multipart', [])
        if isinstance(parts, list) and any(isinstance(part, dict) and matches(part.get('apply')) for part in parts):
            raise error_type('chest renderer requires variants, not multipart applications')
        return definition
    if len(variants) > MAX_VARIANTS:
        return definition
    output, changed = dict(variants), False
    for selector, application in variants.items():
        if not matches(application):
            continue
        if not isinstance(selector, str) or len(selector) > 1023:
            raise error_type('invalid chest selector')
        state = {}
        for item in selector.split(',') if selector else ():
            key, separator, value = item.partition('=')
            if (not separator or not re.fullmatch('[a-z0-9_]+', key)
                    or not re.fullmatch('[a-z0-9_]+', value) or key in state):
                raise error_type('invalid chest selector')
            state[key] = value
        if len(state) > 64 or state.get('facing','north') not in FACINGS or state.get('type','single') not in TYPES:
            raise error_type('invalid chest facing, type, or property count')
        facings = (state['facing'],) if 'facing' in state else (None, *FACINGS)
        types = (state['type'],) if 'type' in state else (None, *TYPES)
        changed = True
        for facing, chest_type in itertools.product(facings, types):
            values = dict(state)
            if facing is not None:
                values['facing'] = facing
            if chest_type is not None:
                values['type'] = chest_type
            key = ','.join(key + '=' + values[key] for key in sorted(values))
            if key in output and output[key] != application:
                raise error_type('overlapping chest state selectors')
            output[key] = application
            if len(output) > MAX_VARIANTS:
                raise error_type('chest expansion exceeds 4096 selectors')
    return {**definition, 'variants': output} if changed else definition


def is_twilight_chest(block_name, model):
    """Only the verified BER identities and untouched entity placeholder path."""
    return (block_name in TWILIGHT_CHESTS and model.get('_builtin_entity') is True
            and 'loader' not in model and 'elements' not in model)


def compile_twilight_chest(compiler, name, model, rotation, selector, depth, error_type, block_name, decode_png):
    # TFChestRenderer registers on vanilla CHEST/TRAPPED_CHEST and overrides
    # only getMaterial. All three vanilla cube layers and world rotations stay.
    if not is_twilight_chest(block_name, model):
        raise error_type('Twilight chest requires a verified unchanged entity model')
    state = selector or {}
    if depth and any(key not in state for key in ('facing', 'type')):
        raise error_type('nested chest requires explicit facing and type palette properties')
    facing, chest_type = state.get('facing', 'north'), state.get('type', 'single')
    if facing not in FACINGS or chest_type not in TYPES:
        raise error_type('invalid chest facing or type')
    if compiler.element_count + 18 > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled chest polygon budget exceeded')
    material, trapped = TWILIGHT_CHESTS[block_name]
    suffix = ('trapped' if chest_type == 'single' else 'trapped_' + chest_type) if trapped else chest_type
    texture = compiler.texture('twilightforest:entity/chest/' + material + '/' + suffix)
    checked = getattr(compiler, '_twilight_chest_checked_textures', None)
    if checked is None:
        checked = compiler._twilight_chest_checked_textures = set()
    if texture not in checked:
        _, _, pixels = decode_png(compiler.textures[texture][0])
        if any(alpha not in (0, 255) for alpha in pixels[3::4]):
            raise error_type('chest cutout textures require binary alpha (0 or 255): ' + texture)
        checked.add(texture)
    polygons = _closed_geometry(chest_type, facing, texture, texture)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: closed Twilight Forest chest; live lid opening animation is not evaluated')
    if any(rotation) or model.get('transform', {}):
        compiler.warnings.add(f'{label}: supplied rotations, UV lock and root transforms are ignored by the shipped chest block-entity renderer')
    compiler.element_count += len(polygons)
    return {'rotation': (0,0,0), 'elements': [], 'polygons': polygons, 'fixed_rotation': True}
