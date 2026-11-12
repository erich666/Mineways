"""Static Modern Industrialization machine models from supplied assets.

Verified against Modern Industrialization 2.5.10 MachineBakedModel,
MachineUnbakedModel.OverlaysJson, UseBlockModelUnbakedModel and ModelHelper.
The machine's fallback ModelData is north-facing, inactive and has no output.
No mod classes are loaded or executed by this module.
"""
import re


LOADERS = ('modern_industrialization:machine',
           'modern_industrialization:use_block_model')


def _resource(value, error_type, default_namespace='minecraft'):
    if not isinstance(value, str):
        raise error_type('machine resource reference must be a string')
    value = value if ':' in value else default_namespace + ':' + value
    if (len(value) > 1023 or not re.fullmatch(r'[a-z0-9_.-]+:[a-z0-9_./-]+', value)
            or any(part in ('', '.', '..') for part in value.split(':')[1].split('/'))):
        raise error_type('invalid machine resource reference: ' + value)
    return value


def adapt_machine_model(compiler, name, model, selector, error_type=ValueError):
    """Return standard inline geometry, a delegate model name, or None.

    Preserve blockstate application rotations in the caller. A selector cannot
    recover MI's frontDirection: the inspected loader reads it from ModelData,
    not a BlockState property. Do not reinterpret unrelated palette properties.
    """
    loader = model.get('loader')
    if loader not in LOADERS:
        return None
    if any(key in model for key in ('transform', 'visibility')):
        raise error_type('machine root transforms and visibility require explicit replacement models')
    if loader == 'modern_industrialization:use_block_model':
        block = _resource(model.get('block'), error_type)
        namespace, path = block.split(':')
        filename = f'assets/{namespace}/blockstates/{path}.json'
        if filename not in compiler.assets.files:
            raise error_type('missing machine casing blockstate: ' + block)
        definition = compiler.assets.json(filename)
        variants = definition.get('variants')
        # The Java loader uses the registered block's defaultBlockState. Asset
        # files alone cannot establish a default among property-based variants.
        if (not isinstance(variants, dict) or set(variants) != {''}
                or 'multipart' in definition):
            raise error_type('machine casing default state is not established by a single generic variant: ' + block)
        application = variants['']
        if (not isinstance(application, dict) or set(application) != {'model'}):
            raise error_type('machine casing delegate needs one unrotated model: ' + block)
        return _resource(application['model'], error_type)

    casing = _resource(model.get('casing'), error_type, 'modern_industrialization')
    namespace, path = casing.split(':')
    casing_model = f'{namespace}:machine_casing/{path}'
    overlays = model.get('default_overlays')
    tiers = model.get('tiered_overlays', {})
    if not isinstance(overlays, dict) or not isinstance(tiers, dict):
        raise error_type('machine overlays must be objects')
    if not isinstance(model.get('no_overlay_on_output_side', False), bool):
        raise error_type('machine no_overlay_on_output_side must be boolean')
    overlays = dict(overlays)
    for tier, fields in tiers.items():
        key = _resource(tier, error_type, 'modern_industrialization')
        if not isinstance(fields, dict):
            raise error_type('machine tier overlays must be objects')
        if key == casing:
            # The Java parser fills null/missing tier fields from the defaults.
            overlays.update((key, value) for key, value in fields.items() if value is not None)

    # getSprite uses NORTH's 2D direction index (2). Its inactive slots select
    # front/left/back/right with side fallback, then top_n/top and bottom_n/bottom.
    fields_by_face = {'north': ('front', 'side'), 'west': ('left', 'side'),
                      'south': ('back', 'side'), 'east': ('right', 'side'),
                      'up': ('top_n', 'top'), 'down': ('bottom_n', 'bottom')}
    planes = {'north': (2, -0.008), 'south': (2, 16.008),
              'west': (0, -0.008), 'east': (0, 16.008),
              'down': (1, -0.008), 'up': (1, 16.008)}
    elements = []
    for face, fields in fields_by_face.items():
        texture = next((overlays[field] for field in fields if overlays.get(field) is not None), None)
        if texture is None:
            continue
        texture = _resource(texture, error_type)
        start, end = [0, 0, 0], [16, 16, 16]
        axis, position = planes[face]
        start[axis] = end[axis] = position
        # ModelHelper.lockUvs matches standard Minecraft full-face UVs. Its
        # Z_OFFSET is 0.0005 blocks, applied outward only along the face normal.
        elements.append({'from': start, 'to': end, 'faces': {
            face: {'texture': texture, 'uv': [0, 0, 16, 16], 'cullface': face}}})
    children = {'casing': {'parent': casing_model}}
    if elements:
        children['overlays'] = {'elements': elements}
    compiler.warnings.add(f'{name}: machine uses supplied casing and north-facing inactive overlays; '
                          'block entity orientation, activity, casing upgrades and output/extraction overlays are unavailable')
    return {'loader': 'neoforge:composite', 'children': children}
