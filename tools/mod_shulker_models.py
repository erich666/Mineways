"""Sophisticated Storage 1.6.6.2181 closed shulkers, from shipped renderer data.

Minecraft 1.21.1 ShulkerModel.parts() renders only the base and lid. Cube UVs
follow ModelPart.Cube/Polygon, including the reversed UP rectangle. The block
entity renderer supplies facing, pose, and tier independently of model JSON.
"""
import hashlib
import re
import struct

LOADER = 'sophisticatedstorage:shulker_box'
FACINGS = ('down', 'up', 'north', 'south', 'west', 'east')
TIERS = {'sophisticatedstorage:' + (tier + '_' if tier != 'base' else '') + 'shulker_box': tier
         for tier in ('base', 'copper', 'iron', 'gold', 'diamond', 'netherite')}
SHRINK = struct.unpack('f', struct.pack('f', .9995))[0]
MAX_GENERATED_PIXELS = 64 * 1024 * 1024
MAX_COMPILED_ELEMENTS = 4000000
MAX_VARIANTS = 4096


def _orient(point, facing):
    a, b, c = point
    return {'up': (a, b, c), 'down': (a, -b, -c), 'north': (-a, -c, -b),
            'south': (a, -c, b), 'west': (-b, -c, a), 'east': (b, -c, -a)}[facing]


def _cube(y, height, texture_v, facing, texture):
    """Return source vertex order and UVs, after pivot and closed renderer pose."""
    low, high = (-8, y, -8), (8, y + height, 8)
    points = [(low[0], low[1], low[2]), (high[0], low[1], low[2]),
              (high[0], high[1], low[2]), (low[0], high[1], low[2]),
              (low[0], low[1], high[2]), (high[0], low[1], high[2]),
              (high[0], high[1], high[2]), (low[0], high[1], high[2])]
    # Width/depth are sixteen. Rectangles use the source 64x64 sheet.
    faces = (((5, 4, 0, 1), (16, texture_v, 32, texture_v + 16)),
             ((2, 3, 7, 6), (32, texture_v + 16, 48, texture_v)),
             ((0, 4, 7, 3), (0, texture_v + 16, 16, texture_v + 16 + height)),
             ((1, 0, 3, 2), (16, texture_v + 16, 32, texture_v + 16 + height)),
             ((5, 1, 2, 6), (32, texture_v + 16, 48, texture_v + 16 + height)),
             ((4, 5, 6, 7), (48, texture_v + 16, 64, texture_v + 16 + height)))
    transformed = [tuple(8 + SHRINK * value for value in _orient((x, -py - 8, -z), facing))
                   for x, py, z in points]
    output = []
    for corners, (u1, v1, u2, v2) in faces:
        # Native polygon UVs use 0..16 units, not normalized 0..1 units.
        uv = ((u2 / 4, v1 / 4), (u1 / 4, v1 / 4), (u1 / 4, v2 / 4), (u2 / 4, v2 / 4))
        output.append({'texture': texture, 'vertices': [(*transformed[index], *coord) for index, coord in zip(corners, uv)]})
    return output


def _texture(compiler, tier, error_type, decode_png):
    base = compiler.texture('sophisticatedstorage:entity/shulker_box/no_tint')
    overlay = compiler.texture('sophisticatedstorage:entity/shulker_box/' + tier + '_tier')
    name = 'mineways:shulker/closed_' + hashlib.sha256((base + '\n' + overlay).encode()).hexdigest()
    if name not in compiler.textures:
        size = 0
        for source in (base, overlay):
            width, _, pixels = decode_png(compiler.textures[source][0])
            # entityCutoutNoCull discards alpha <0.1 rather than blending. The
            # current texture compositor is exact for the supported binary case.
            if any(alpha not in (0, 255) for alpha in pixels[3::4]):
                raise error_type('shulker cutout textures require binary alpha (0 or 255): ' + source)
            size = max(size, width)
        if compiler.face_texture_pixel_count + size * size > MAX_GENERATED_PIXELS:
            raise error_type('shulker generated-texture pixel budget exceeded')
        compiler.overlay_texture(name, base, overlay)
        compiler.face_texture_pixels[name] = size * size
        compiler.face_texture_pixel_count += size * size
    return name


def compile_shulker(compiler, name, model, rotation, selector, depth, error_type, block_name, decode_png):
    if depth >= 63:
        raise error_type('excessive shulker model depth')
    if model.get('loader') != LOADER or block_name not in TIERS:
        raise error_type('shulker renderer requires a verified storage block identity')
    state = selector or {}
    if depth and 'facing' not in state:
        raise error_type('nested dynamic model requires explicit palette flags')
    # Constructors do not override StateDefinition.any(): Direction.DOWN is first.
    facing = state.get('facing', 'down')
    if facing not in FACINGS:
        raise error_type('shulker facing must be a cardinal direction')
    if compiler.element_count + 12 > MAX_COMPILED_ELEMENTS:
        raise error_type('compiled shulker polygon budget exceeded')
    texture = _texture(compiler, TIERS[block_name], error_type, decode_png)
    polygons = _cube(-8, 8, 28, facing, texture) + _cube(-16, 12, 0, facing, texture)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: closed undyed shulker with visible tier; saved dyes, tier hiding, opening animation, locks, upgrades and displayed items are not evaluated')
    if any(rotation) or model.get('transform', {}):
        compiler.warnings.add(f'{label}: supplied rotations, UV lock and root transforms are ignored by the shipped shulker block-entity renderer')
    compiler.element_count += len(polygons)
    return {'rotation': (0, 0, 0), 'elements': [], 'polygons': polygons, 'fixed_rotation': True}


def expand_shulker(compiler, name, definition, error_type):
    """Supply the palette facings omitted by the renderer-only source variants."""
    def matches(application):
        chosen = application[0] if isinstance(application, list) and application else application
        if not isinstance(chosen, dict) or 'model' not in chosen:
            return False
        try:
            return compiler.model(chosen['model']).get('loader') == LOADER
        except error_type:
            return False  # The normal compile path reports invalid models.

    variants = definition.get('variants')
    if not isinstance(variants, dict):
        parts = definition.get('multipart', [])
        if isinstance(parts, list) and any(isinstance(part, dict) and matches(part.get('apply')) for part in parts):
            raise error_type('shulker renderer requires variant models, not multipart applications')
        return definition
    if len(variants) > MAX_VARIANTS:
        return definition
    output, changed = dict(variants), False
    for selector, application in variants.items():
        if not matches(application):
            continue
        state = {}
        for item in selector.split(',') if isinstance(selector, str) and selector else []:
            key, separator, value = item.partition('=')
            if not separator or not re.fullmatch(r'[a-z0-9_]+', key) or not re.fullmatch(r'[a-z0-9_]+', value) or key in state:
                raise error_type('invalid shulker state selector')
            state[key] = value
        if not isinstance(selector, str):
            raise error_type('invalid shulker state selector')
        if 'facing' in state:
            if state['facing'] not in FACINGS:
                raise error_type('shulker facing must be a cardinal direction')
            continue
        changed = True
        for facing in FACINGS:
            values = {**state, 'facing': facing}
            key = ','.join(key + '=' + values[key] for key in sorted(values))
            if key in output and output[key] != application:
                raise error_type('overlapping shulker state selectors')
            output[key] = application
            if len(output) > MAX_VARIANTS:
                raise error_type('shulker expansion exceeds 4096 selectors')
    return {**definition, 'variants': output} if changed else definition
