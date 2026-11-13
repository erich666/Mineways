"""Deterministic static CloudFX from XyCraft Core 0.7.53's shipped algorithm.

CloudFX.uploadFirstFrame warms128steps, then tickAndUpload generates step129.
Ticker.evolveNoise uses float32 arithmetic, in-place blue/alpha feedback and a
JOML1.10.5 xoroshiro128+ generator. The original uses a clock-based random seed;
Mineways fixes seed0 so identical sources compile to identical pixels.
"""
import math
import struct

CLOUD_IDS = ('xycraft_core:block/cloudfx', 'xycraft_core:block/cloudfx_translucent',
             'xycraft_core:block/cloudfx_intense')
MAX_SIZE = 64
SEED = 0
STEPS = 129
_MASK = (1 << 64) - 1
_FLOAT = struct.Struct('<f')


def _f32(value):
    return _FLOAT.unpack(_FLOAT.pack(value))[0]


class _Random:
    """JOML Random(long): SplitMix64 initialization, xoroshiro128+ nextFloat."""
    def __init__(self, seed):
        state = seed & _MASK
        words = []
        for _ in range(2):
            state = (state + 0x9E3779B97F4A7C15) & _MASK
            value = ((state ^ (state >> 30)) * 0xBF58476D1CE4E5B9) & _MASK
            value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & _MASK
            words.append(value ^ (value >> 31))
        self.s0, self.s1 = words

    def next_float(self):
        a, b = self.s0, self.s1
        value = (a + b) & 0xFFFFFFFF
        b ^= a
        self.s0 = (((a << 55) | (a >> 9)) ^ b ^ (b << 14)) & _MASK
        self.s1 = ((b << 36) | (b >> 28)) & _MASK
        return (value >> 8) / 16777216.0


def generate(spec, error_type=ValueError):
    """Return width,height,RGBA bytes; bounded to one power-of-two square≤64."""
    if not isinstance(spec, dict):
        raise error_type('CloudFX declaration must be an object')
    width, height = spec.get('width'), spec.get('height')
    if (type(width) is not int or type(height) is not int or width != height
            or not 1 <= width <= MAX_SIZE or width & (width - 1)):
        raise error_type('CloudFX requires square power-of-two dimensions 1..64')
    opacity, intensity = spec.get('alpha'), spec.get('min_intensity')
    if type(opacity) is not int or not -(1 << 31) <= opacity < (1 << 31):
        raise error_type('CloudFX alpha must be a 32-bit integer')
    if (type(intensity) not in (int, float) or not 0 <= intensity <= 1
            or not math.isfinite(intensity)):
        raise error_type('CloudFX min_intensity must be finite and within 0..1')
    threshold = _f32(intensity)
    count = width * height
    red, green, blue, alpha = ([0.] * count for _ in range(4))
    random = _Random(SEED)
    mask = width - 1
    offset, weight = _f32(-0.0215), _f32(0.1005)
    feedback, decay = _f32(0.0108), _f32(0.0425)
    for _ in range(STEPS):
        for x in range(width):
            next_x = (x + 1) & mask
            for y in range(height):
                row, next_row = y * width, ((y + 1) & mask) * width
                index = row + x
                total = offset
                for nx in range(x - 1, x + 2):
                    for ny in range(y - 1, y + 2):
                        total = _f32(total + red[(nx & mask) + (ny & mask) * width])
                average = _f32(blue[index] + blue[next_x + row])
                average = _f32(average + blue[next_x + next_row])
                average = _f32(average + blue[x + next_row])
                average = _f32(average * 0.25)
                green[index] = _f32(_f32(total * weight) + average)
                blue[index] = _f32(blue[index] + _f32(alpha[index] * feedback))
                alpha[index] = _f32(alpha[index] - decay)
                if blue[index] < 0:
                    blue[index] = 0.
                if random.next_float() < threshold:
                    alpha[index] = 1.
        red, green = green, red
    pixels = bytearray(count * 4)
    for index, value in enumerate(red):
        intensity = min(1., max(_f32(0.1), _f32(value * 1.5)))
        gray = int(_f32(intensity * 255.)) & 255
        pixels[index * 4:index * 4 + 4] = bytes((gray, gray, gray, opacity & 255))
    return width, height, bytes(pixels)


def _matches(pattern, value, error_type):
    """Bounded subset of Java Pattern.asPredicate (find), with no backtracking."""
    if pattern is None:
        return True
    if not isinstance(pattern, str) or len(pattern) > 128:
        raise error_type('CloudFX atlas filter pattern exceeds supported bounds')
    start, end = pattern.startswith('^'), pattern.endswith('$')
    pattern = pattern[int(start):len(pattern) - int(end) if end else len(pattern)]
    tokens = [] if start else [None]
    index = 0
    while index < len(pattern):
        char = pattern[index]
        if char == '.' and pattern[index:index + 2] == '.*':
            tokens.append(None)
            index += 2
            continue
        if char == '\\' and index + 1 < len(pattern):
            index += 1
            char = pattern[index]
            if char not in r'.^$*+?{}[]\|()':
                raise error_type('unsupported CloudFX atlas filter escape')
        elif char == '.':
            char = True
        elif char not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-':
            raise error_type('unsupported CloudFX atlas filter expression; supply an explicit PNG')
        tokens.append(char)
        index += 1
    if not end:
        tokens.append(None)
    previous = [True] + [False] * len(tokens)
    for i, token in enumerate(tokens):
        if token is None:
            previous[i + 1] = previous[i]
    for char in value:
        current = [False] * (len(tokens) + 1)
        for i, token in enumerate(tokens):
            current[i + 1] = ((current[i] or previous[i + 1]) if token is None else
                              previous[i] and (token is True or token == char))
        previous = current
    return previous[-1]


def declarations(assets, resource, asset_path, error_type=ValueError):
    """Resolve the three known sprites from the ordered block-atlas source stack.

    SpriteSourceList concatenates every blocks.json source list. Output.add
    replaces an earlier supplier for the same ID, and filters remove suppliers.
    Explicit same-name PNG precedence is handled by the caller as user policy.
    """
    result = {}
    total = 0
    for entry in assets.atlas_sources:
        data = assets.json('assets/minecraft/atlases/blocks.json', entry)
        sources = data.get('sources')
        if not isinstance(sources, list):
            raise error_type('block atlas sources must be a list')
        total += len(sources)
        if total > 4096:
            raise error_type('CloudFX atlas source count exceeds 4096')
        for source in sources:
            if not isinstance(source, dict):
                raise error_type('invalid block atlas source')
            kind = resource(source.get('type'))
            if kind == 'xycraft_core:fxsprite':
                name = resource(source.get('id'))
                if name in CLOUD_IDS:
                    result[name] = {key: source.get(key) for key in
                                    ('width', 'height', 'alpha', 'min_intensity')}
            elif kind == 'minecraft:single':
                original = resource(source.get('resource'))
                name = resource(source.get('sprite', original))
                if name in CLOUD_IDS and asset_path(original, 'textures', '.png') in assets.files:
                    result[name] = {'resource': original}
            elif kind in ('minecraft:directory', 'neoforge:namespaced_directory'):
                directory, prefix = source.get('source'), source.get('prefix')
                if not isinstance(directory, str) or not isinstance(prefix, str):
                    raise error_type('invalid block atlas directory source')
                for name in CLOUD_IDS:
                    namespace, path = name.split(':')
                    if kind == 'neoforge:namespaced_directory' and source.get('namespace') != namespace:
                        continue
                    if path.startswith(prefix):
                        original = resource(namespace + ':' + (directory + '/' if directory else '') + path[len(prefix):])
                        if asset_path(original, 'textures', '.png') in assets.files:
                            result[name] = {'resource': original}
            elif kind == 'minecraft:filter':
                pattern = source.get('pattern')
                if not isinstance(pattern, dict):
                    raise error_type('invalid CloudFX atlas filter')
                for name in list(result):
                    namespace, path = name.split(':')
                    if (_matches(pattern.get('namespace'), namespace, error_type)
                            and _matches(pattern.get('path'), path, error_type)):
                        del result[name]
    return result
