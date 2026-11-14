"""Sophisticated Backpacks' supplied parts, with verified placed-block defaults.

BackpackDynamicModel creates fixed part parents and forwards only `clips`.
BackpackBakedModel selects pouches/tanks/battery from the block state. Saved
colors, fluid, charge, displayed items and animation are separate render data.
"""
import copy

LOADER = 'sophisticatedbackpacks:backpack'
DEFAULT_COLORS = (0xffcc613a, 0xff622e1a)


def _clips(compiler, model, error_type):
    textures = model.get('textures', {})
    value = '#clips'
    seen = set()
    while isinstance(value, str) and value.startswith('#'):
        if value in seen:
            return None  # BlockModel.hasTexture also treats cyclic aliases as missingno.
        if len(seen) >= 64:
            raise error_type('excessive backpack clips texture aliases')
        seen.add(value)
        if value[1:] not in textures:
            return None  # BlockModel.hasTexture resolves missing aliases to missingno.
        value = textures[value[1:]]
    if value in ('missingno', 'minecraft:missingno'):
        return None
    # Reuse the compiler's resource validation, without resolving the image yet.
    return compiler.loader_texture({'textures': {'clips': value}}, 'clips')


def compile_backpack(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    flags = {}
    for flag in ('left_tank', 'right_tank', 'battery'):
        value = (selector or {}).get(flag, 'false')
        if value not in ('true', 'false'):
            raise error_type('backpack flags must be true or false')
        flags[flag] = value == 'true'
    clips = _clips(compiler, model, error_type)
    parts = ('base', 'left_tank' if flags['left_tank'] else 'left_pouch',
             'right_tank' if flags['right_tank'] else 'right_pouch',
             'battery' if flags['battery'] else 'front_pouch', 'straps')
    children = {}
    total = 0
    for part in parts:
        reference = 'sophisticatedbackpacks:block/backpack_' + part
        source = compiler.model(reference)
        if source.get('loader') or source.get('transform', {}) != {}:
            raise error_type('backpack part requires standard geometry without root transforms')
        elements = source.get('elements')
        if not isinstance(elements, list) or not elements or len(elements) > 2048:
            raise error_type('backpack part requires bounded supplied elements')
        total += len(elements)
        if total > 2048:
            raise error_type('backpack combined element count exceeds limit')
        textures = dict(source.get('textures', {}))
        if clips is not None:
            textures['clips'] = clips
        elements = copy.deepcopy(elements)
        for element in elements:
            if not isinstance(element, dict) or not isinstance(element.get('faces'), dict):
                raise error_type('invalid backpack part element')
            for face in element['faces'].values():
                if not isinstance(face, dict):
                    raise error_type('invalid backpack part face')
                texture = face.get('texture')
                seen = set()
                while isinstance(texture, str) and texture.startswith('#'):
                    if texture in seen or len(seen) >= 64:
                        raise error_type('cyclic backpack part texture')
                    seen.add(texture)
                    if texture[1:] not in textures:
                        texture = compiler.missing_texture(texture)
                        break
                    texture = textures[texture[1:]]
                texture = compiler.loader_texture({'textures': {'face': texture}}, 'face')
                compiler.texture(texture)
                texture = compiler.face_color_texture(texture, element, face, reference)
                tint = face.get('tintindex', -1)
                if type(tint) is not int:
                    raise error_type('backpack tint index must be an integer')
                if tint in (0, 1):
                    texture = compiler.face_color_texture(texture, {},
                        {'neoforge_data': {'color': DEFAULT_COLORS[tint]}}, reference)
                face['texture'] = texture
                for key in ('neoforge_data', 'forge_data', 'tintindex'):
                    face.pop(key, None)
            element.pop('neoforge_data', None)
            element.pop('forge_data', None)
        children[part] = {'elements': elements, 'textures': textures}
    x, y, uvlock = rotation[:3]
    application = {'model': {'loader': 'neoforge:composite', 'children': children},
                   'x': x, 'y': y, 'uvlock': bool(uvlock)}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: default backpack colors with palette-selected pouches, tanks and battery; saved recoloring, fluids, charge, displayed items and animation are not evaluated')
    return result
