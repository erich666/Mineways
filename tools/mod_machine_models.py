"""Static Modern Industrialization machine models from supplied assets.

Verified against Modern Industrialization 2.5.10 MachineBakedModel,
MachineUnbakedModel.OverlaysJson, UseBlockModelUnbakedModel and ModelHelper.
The machine's fallback ModelData is north-facing, inactive and has no output.
No mod classes are loaded or executed by this module.
"""
import re


LOADERS = ('modern_industrialization:machine',
           'modern_industrialization:use_block_model')


# MI 2.5.10 FluidDefinition and EI MIFluidHolder register empty MIFluidBlock cells.
# Keep this identity set narrow: Create/Copycats also use air assets for visible renderers.
EMPTY_FLUID_BLOCKS = frozenset(
    {'modern_industrialization:' + name for name in (
        'acetylene acrylic_acid acrylic_glue argon benzene biodiesel boosted_diesel butadiene caprolactam '
        'chlorine chromium_hydrochloric_solution creosote crude_oil cryofluid deuterium diesel diethyl_ether '
        'ethanol ethylbenzene ethylene heavy_fuel heavy_water heavy_water_steam helium helium_3 helium_plasma '
        'high_pressure_heavy_water high_pressure_heavy_water_steam high_pressure_steam high_pressure_water '
        'hydrochloric_acid hydrogen light_fuel liquid_air lubricant manganese_sulfuric_solution methane '
        'molten_redstone naphtha nitrogen nylon oxygen plant_oil platinum_sulfuric_solution polyethylene '
        'polyvinyl_chloride propene purified_platinum_sulfuric_solution raw_biodiesel raw_synthetic_oil '
        'shale_oil sodium_hydroxide soldering_alloy steam steam_cracked_heavy_fuel steam_cracked_light_fuel '
        'steam_cracked_naphtha styrene styrene_butadiene styrene_butadiene_rubber sugar_solution '
        'sulfuric_acid sulfuric_crude_oil sulfuric_heavy_fuel sulfuric_light_fuel sulfuric_naphtha '
        'synthetic_oil synthetic_rubber toluene tritium uu_matter vinyl_chloride '
    ).split()}
    | {'extended_industrialization:' + name for name in (
        'blazing_essence composted_manure distilled_water honey manure npk_fertilizer phosphoric_acid '
        'potassium_chloride potassium_hydroxide '
    ).split()}
)


def is_empty_fluid(block_name, name, model):
    return (block_name in EMPTY_FLUID_BLOCKS and name == "minecraft:block/air"
            and model == {"textures": {}})


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

    The Java geometry ignores ModelState, so the caller must use identity.
    A selector cannot
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


def compile_machine(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    """Both shipped MI loaders bake their captured defaults without ModelState."""
    if depth >= 63:
        raise error_type('excessive machine model depth')
    adapted = adapt_machine_model(compiler, name, model, selector, error_type)
    if adapted is None:
        raise error_type('unsupported machine model loader')
    result = compiler.compile_model({'model': adapted}, depth + 1, selector, block_name)
    result['fixed_rotation'] = True
    return result
