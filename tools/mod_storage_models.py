"""Asset-backed storage bases verified against shipped Java loaders, without Java execution.

Refined Storage 2.0.9 DiskInterface/DriveUnbakedGeometry select supplied base JSONs.
Their BakedModels rotate by BiDirection and append disks from ModelData separately.
Cable Tiers 0.6.15 uses the same pattern with a tier-specific active/inactive base.
ExtendedAE 2.2.41 ExDriveModel bakes extended_me_drive_base with the blockstate
transform, then ExDriveBakedModel appends cell chassis from DriveModelData.STATE.
"""
import itertools
import re

COLORS = ('white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray',
          'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')
TIERS = ('elite', 'ultra', 'mega', 'creative')
# Exact equivalents of BiDirection's Quaternion.rotateXYZ in blockstate X/Y order.
DIRECTIONS = {'north': (0, 0), 'east': (0, 90), 'south': (0, 180), 'west': (0, 270),
              'up_north': (270, 180), 'up_east': (270, 270),
              'up_south': (270, 0), 'up_west': (270, 90),
              'down_north': (90, 0), 'down_east': (90, 90),
              'down_south': (90, 180), 'down_west': (90, 270)}
MAX_VARIANTS = 4096
LOADERS = {'refinedstorage:disk_interface', 'refinedstorage:disk_drive', 'extendedae:ex_drive'} | {
    f'cabletiers:{tier}_disk_interface' for tier in TIERS}


def _model(compiler, reference, error_type):
    if not isinstance(reference, str):
        return None
    namespace, separator, path = reference.partition(':')
    if not separator or namespace not in ('refinedstorage', 'cabletiers', 'extendedae'):
        return None
    if (not re.fullmatch('[a-z0-9_./-]+', path) or len(reference) > 1023
            or any(part in ('', '.', '..') for part in path.split('/'))):
        raise error_type('invalid storage model reference: ' + reference)
    source = f'assets/{namespace}/models/{path}.json'
    if source not in compiler.assets.files:
        return None
    # This cache contains only immutable supplied JSON. Do not resolve a custom
    # loader through Compiler.model(), which correctly rejects unknown loaders.
    cache = getattr(compiler, '_storage_model_sources', None)
    if cache is None:
        cache = compiler._storage_model_sources = {}
    if source not in cache:
        cache[source] = compiler.assets.json(source)
    model = cache[source]
    loader = model.get('loader')
    return model if isinstance(loader, str) and loader in LOADERS else None


def _properties(selector, error_type):
    if not isinstance(selector, str) or len(selector) > 1023:
        raise error_type('invalid storage state selector')
    result = {}
    for pair in selector.split(',') if selector else ():
        key, separator, value = pair.partition('=')
        if (not separator or not re.fullmatch('[a-z0-9_]+', key)
                or not re.fullmatch('[a-z0-9_]+', value) or key in result):
            raise error_type('invalid storage state selector: ' + selector)
        result[key] = value
        if len(result) > 64:
            raise error_type('storage state has more than 64 properties')
    return result


def expand_block(compiler, name, definition, error_type):
    """Replace inspected loaders with supplied standard models and explicit states."""
    variants = definition.get('variants')
    if not isinstance(variants, dict) or not variants or len(variants) > MAX_VARIANTS:
        return definition
    expanded = {}
    changed = False
    for selector, application in variants.items():
        model = _model(compiler, application.get('model'), error_type) if isinstance(application, dict) else None
        if model is None:
            if selector in expanded and expanded[selector] != application:
                raise error_type('overlapping storage state selectors')
            expanded[selector] = application
            continue
        if any(key in model for key in ('transform', 'visibility')):
            raise error_type('storage loader transforms and visibility require an explicit replacement model')
        loader = model['loader']
        changed = True
        if loader == 'extendedae:ex_drive':
            expanded[selector] = {**application, 'model': 'extendedae:block/extended_drive/extended_me_drive_base'}
            compiler.warnings.add(f'{name}: supplied ExtendedAE drive base only; cell chassis, installed cells and indicators require block-entity data')
            continue
        if (any(type(application.get(axis, 0)) is not int or application.get(axis, 0) % 360 for axis in ('x', 'y'))
                or application.get('uvlock', False) is not False or 'ae2:z' in application):
            raise error_type('storage loader application transforms require an explicit replacement model')
        state = _properties(selector, error_type)
        if 'direction' in state and state['direction'] not in DIRECTIONS:
            raise error_type('invalid storage direction: ' + state['direction'])
        drive = loader == 'refinedstorage:disk_drive'
        if drive:
            active_model = inactive_model = 'refinedstorage:block/disk_drive/base'
        else:
            color = model.get('color')
            if color not in COLORS:
                raise error_type('invalid storage interface color')
            if loader.startswith('cabletiers:'):
                tier = model.get('tier')
                if tier not in TIERS or loader != f'cabletiers:{tier}_disk_interface':
                    raise error_type('invalid Cable Tiers disk interface tier')
                prefix = f'cabletiers:block/{tier}_disk_interface/'
            else:
                prefix = 'refinedstorage:block/disk_interface/'
            active_model, inactive_model = prefix + 'base_' + color, prefix + 'inactive'
            if 'active' in state and state['active'] not in ('true', 'false'):
                raise error_type('invalid storage active state')
        compiler.warnings.add(f'{name}: supplied storage base only; installed disks and content indicators require block-entity data')
        if not drive:
            compiler.warnings.add(f'{name}: active-state textures are preserved; custom emissive lighting is not evaluated')
        directions = (state['direction'],) if 'direction' in state else DIRECTIONS
        active_values = (None,) if drive else ((state['active'],) if 'active' in state else ('false', 'true'))
        for direction, active in itertools.product(directions, active_values):
            values = {**state, 'direction': direction}
            if active is not None:
                values['active'] = active
            key = ','.join(f'{key}={values[key]}' for key in sorted(values))
            x, y = DIRECTIONS[direction]
            value = {**application, 'model': active_model if active == 'true' else inactive_model, 'x': x, 'y': y}
            if key in expanded and expanded[key] != value:
                raise error_type('overlapping storage state selectors')
            expanded[key] = value
            if len(expanded) > MAX_VARIANTS:
                raise error_type('storage expansion exceeds 4096 selectors')
        # Verified default: BiDirectionType.NORTH, active=false. Keep a fallback
        # only for the original generic selector; explicit variants stay explicit.
        if selector == '':
            expanded[''] = {**application, 'model': inactive_model, 'x': 0, 'y': 0}
        if len(expanded) > MAX_VARIANTS:
            raise error_type('storage expansion exceeds 4096 selectors')
    return {**definition, 'variants': expanded} if changed else definition


SIMPLE_MATERIAL_LOADER = 'sophisticatedstorage:simple_material'

def compile_simple_material(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Bake the supplied no-material base, as SimpleMaterialModel.Baked does.

    Verified against Sophisticated Storage 1.6.6.2181. The loader deserializes
    base and overlay independently. The outer model's textures do not become
    the base's textures. Without MATERIAL model data only the base is emitted.
    """
    base = model.get('base')
    if not isinstance(base, dict):
        raise error_type('simple material model requires an inline base model')
    if 'overlay' in model and not isinstance(model['overlay'], dict):
        raise error_type('simple material overlay must be an inline model')
    resolved_base = compiler.model(base)
    if any(owner.get('transform', {}) != {} for owner in (model, resolved_base)):
        raise error_type('simple material root transforms require an explicit replacement model')
    x, y, uvlock = rotation[:3]
    application = {'model': base, 'x': x, 'y': y, 'uvlock': bool(uvlock)}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: supplied default storage base only; custom block-entity materials and their overlays are not evaluated')
    return result


LIMITED_BARREL_LOADER = 'sophisticatedstorage:limited_barrel'


def _barrel_resource(value, error_type):
    if not isinstance(value, str) or len(value) > 1023:
        raise error_type('invalid limited barrel resource')
    namespace, separator, path = value.partition(':')
    if not separator:
        namespace, path = 'minecraft', namespace
    if (not re.fullmatch('[a-z0-9_.-]+', namespace)
            or not re.fullmatch('[a-z0-9_./-]+', path)
            or any(part in ('', '.', '..') for part in path.split('/'))):
        raise error_type('invalid limited barrel resource: ' + value)
    return namespace + ':' + path


def _barrel_parts(value, error_type):
    if not isinstance(value, dict) or len(value) > 32:
        raise error_type('invalid or excessive limited barrel parts')
    result = {}
    for name, definition in value.items():
        if not isinstance(name, str) or len(name) > 64 or not isinstance(definition, dict):
            raise error_type('invalid limited barrel part')
        textures = definition.get('textures', {})
        if not isinstance(textures, dict) or len(textures) > 256:
            raise error_type('invalid or excessive limited barrel texture overrides')
        copied = {}
        for key, texture in textures.items():
            if not isinstance(key, str) or not re.fullmatch('[a-z0-9_./-]{1,255}', key):
                raise error_type('invalid limited barrel texture key')
            # The shipped loader turns # aliases into literal reference/ materials
            # on these non-partitioned parts. Do not reinterpret them as vanilla aliases.
            if isinstance(texture, str) and texture.startswith('#'):
                copied[key] = _barrel_resource('minecraft:reference/' + texture[1:], error_type)
            else:
                copied[key] = _barrel_resource(texture, error_type)
        result[name] = {'textures': copied}
        if 'model' in definition:
            result[name]['model'] = _barrel_resource(definition['model'], error_type)
    return result


def _merge_barrel_parts(parent, own):
    result = dict(parent)
    for key, part in own.items():
        prior = parent.get(key, {})
        result[key] = {**prior, **part, 'textures': {**prior.get('textures', {}), **part['textures']}}
    return result


def _limited_barrel_parts(compiler, reference, error_type, chain=()):
    """Merge each layer's wood overrides before that layer's parent defaults."""
    inline = isinstance(reference, dict)
    key = None if inline else _barrel_resource(reference, error_type)
    if len(chain) >= 64 or (key is not None and key in chain):
        raise error_type('cyclic or excessive limited barrel parents')
    cache = getattr(compiler, '_limited_barrel_part_cache', None)
    if cache is None:
        cache = compiler._limited_barrel_part_cache = {}
    if key in cache:
        return cache[key]
    if inline:
        own = reference
    else:
        namespace, path = key.split(':')
        own = compiler.assets.json(f'assets/{namespace}/models/{path}.json')
    if not isinstance(own, dict) or own.get('loader', LIMITED_BARREL_LOADER) != LIMITED_BARREL_LOADER:
        raise error_type('limited barrel parent must use the limited barrel loader')
    if own.get('transform', {}) != {}:
        raise error_type('limited barrel root transforms require an explicit replacement model')
    parts = _barrel_parts(own.get('model_parts', {}), error_type)
    overrides = own.get('wood_overrides', {})
    if not isinstance(overrides, dict) or len(overrides) > 256:
        raise error_type('invalid or excessive limited barrel wood overrides')
    for wood, value in overrides.items():
        if not isinstance(wood, str) or not re.fullmatch('[a-z0-9_.:-]{1,255}', wood):
            raise error_type('invalid limited barrel wood override name')
        parsed = _barrel_parts(value, error_type)
        if wood == 'acacia':
            parts = _merge_barrel_parts(parts, parsed)
    if 'parent' in own:
        parent = _limited_barrel_parts(compiler, own['parent'], error_type, chain + (key,))
        parts = _merge_barrel_parts(parent, parts)
    if len(parts) > 32:
        raise error_type('limited barrel inherited parts exceed limit')
    for part in parts.values():
        if len(part['textures']) > 256:
            raise error_type('limited barrel inherited textures exceed limit')
    if key is not None:
        cache[key] = parts
    return parts



def _barrel_geometry(compiler, reference, error_type, chain=(), visits=None):
    """CompositeElementsModel flattens SimpleCompositeModel, then overrides textures.

    This differs from baking a normal composite: child visibility and child root
    transforms are not consulted by getElements(). All elements use one texture map.
    """
    if visits is None:
        visits = [0]
    visits[0] += 1
    key = None if isinstance(reference, dict) else _barrel_resource(reference, error_type)
    if len(chain) >= 64 or visits[0] > 4096 or (key is not None and key in chain):
        raise error_type('cyclic or excessive limited barrel geometry')
    if key is None:
        own = reference
    else:
        namespace, path = key.split(':')
        path = f'assets/{namespace}/models/{path}.json'
        own = compiler.assets.json(path) if path in compiler.assets.files else compiler.model(key)
    if not isinstance(own, dict) or not isinstance(own.get('textures', {}), dict):
        raise error_type('invalid limited barrel geometry model')
    loader = own.get('loader')
    if loader not in (None, 'sophisticatedstorage:simple_composite'):
        raise error_type('unsupported limited barrel part loader: ' + str(loader))
    parent = _barrel_geometry(compiler, own['parent'], error_type, chain + (key,), visits) if 'parent' in own else {}
    own_elements = own.get('elements', [])
    if not isinstance(own_elements, list) or len(own_elements) > 2048:
        raise error_type('invalid or excessive limited barrel elements')
    standard_elements = own_elements or parent.get('standard_elements', [])
    custom_elements = list(parent.get('custom_elements', [])) if loader is None else []
    custom_textures = dict(parent.get('custom_textures', {})) if loader is None else {}
    custom = loader == 'sophisticatedstorage:simple_composite' or parent.get('custom', False)
    if loader is not None:
        children = own.get('parts')
        if not isinstance(children, dict) or not 1 <= len(children) <= 256:
            raise error_type('simple composite requires 1..256 inline parts')
        for child in children.values():
            if not isinstance(child, dict):
                raise error_type('simple composite part must be an inline model')
            parsed = _barrel_geometry(compiler, child, error_type, chain + (key,), visits)
            if len(standard_elements) + len(custom_elements) + len(parsed['elements']) > 2048:
                raise error_type('limited barrel composite element count exceeds limit')
            custom_elements.extend(parsed['elements'])
            # SimpleCompositeModel.getTextures uses putIfAbsent, in child order.
            for texture_map in (parsed['own_textures'], parsed['custom_textures'] if parsed['custom'] else parsed['parent_textures']):
                for name, texture in texture_map.items():
                    custom_textures.setdefault(name, texture)
    textures = {**parent.get('textures', {}), **custom_textures, **own.get('textures', {})}
    if max(len(textures), len(custom_textures)) > 4096:
        raise error_type('limited barrel geometry texture count exceeds limit')
    if len(standard_elements) + len(custom_elements) > 2048:
        raise error_type('limited barrel composite element count exceeds limit')
    return {'elements': list(standard_elements) + custom_elements, 'textures': textures, 'custom': custom,
            'standard_elements': standard_elements, 'custom_elements': custom_elements,
            'custom_textures': custom_textures, 'own_textures': own.get('textures', {}),
            'parent_textures': parent.get('own_textures', {})}

def compile_limited_barrel(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Default acacia, visible tier, no dyes/materials/items, from shipped models.

    WoodStorageBlockEntity defaults to ACACIA and StorageBlockEntity defaults
    showTier to true. The supplied variant chooses flat/nonflat and orientation.
    Raw parent layers matter: generic BlockModel merging loses per-part defaults.
    """
    parts = _limited_barrel_parts(compiler, name, error_type)
    children = {}
    for part_name in ('base', 'tier'):
        part = parts.get(part_name)
        if part is None and part_name == 'tier':
            continue
        if part is None or 'model' not in part:
            raise error_type('limited barrel ' + part_name + ' has no supplied model')
        geometry = _barrel_geometry(compiler, part['model'], error_type)
        children[part_name] = {'elements': geometry['elements'],
                               'textures': {**geometry['textures'], **part['textures']}}
    x, y, uvlock = rotation[:3]
    application = {'model': {'loader': 'neoforge:composite', 'children': children},
                   'x': x, 'y': y, 'uvlock': bool(uvlock)}
    if len(rotation) == 4:
        application['ae2:z'] = rotation[3]
    result = compiler.compile_model(application, depth + 1, selector, block_name)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: default acacia barrel with visible tier only; saved wood, dyes, custom materials, packing, lock indicators and displayed items are not evaluated')
    return result
