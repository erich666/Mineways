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
