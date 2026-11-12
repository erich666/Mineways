#!/usr/bin/env python3
"""Compile Minecraft asset folders/JARs/ZIPs to a data-only Mineways mod pack."""

import argparse
import binascii
from collections import OrderedDict
import itertools
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import sys
import tempfile
import zipfile
import zlib

MAX_FILE = 16 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
MAX_FILES = 1000000
MAX_ARCHIVE_FILES = 100000
MAX_ASSET_FILES = 400000
MAX_SOURCES = 1024
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_SOURCE_BYTES = 2 * 1024 * 1024 * 1024
MAX_STATES = 4096
MAX_ELEMENTS = 2048
MAX_PIXELS = 4 * 1024 * 1024
MAX_COMPILED_ELEMENTS = 4000000
MAX_TEXTURES = 131072
MAX_CATALOG_BLOCKS = 100000
MAX_CATALOG_SELECTORS = 1000000
MAX_GEOMETRIES = 250000
MAX_MANIFEST = 512 * 1024 * 1024
RESOURCE = re.compile(r"^[a-z0-9_.-]+:[a-z0-9_./-]+$")
PROPERTY = re.compile(r"^[a-z0-9_]+$")
DIRECTIONS = ('down', 'up', 'north', 'south', 'west', 'east')


class ImportError(ValueError):
    pass


def resource(value):
    if not isinstance(value, str):
        raise ImportError('resource reference must be a string')
    value = value if ':' in value else 'minecraft:' + value
    if len(value) > 1023 or not RESOURCE.fullmatch(value) or any(p in ('', '.', '..') for p in value.split(':')[1].split('/')):
        raise ImportError('invalid resource reference: ' + value)
    return value


def asset_path(identifier, kind, suffix='.json'):
    namespace, name = resource(identifier).split(':')
    return f'assets/{namespace}/{kind}/{name}{suffix}'


def safe_path(name):
    if '\\' in name or '\x00' in name or ':' in name:
        raise ImportError('unsafe archive path: ' + repr(name))
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts:
        raise ImportError('unsafe archive path: ' + repr(name))
    return path.as_posix()


class Assets:
    """Index bounded source metadata; open and decompress only referenced assets."""
    def __init__(self, sources):
        self.files = {}
        self.archives = OrderedDict()
        self.archive_stamps = {}
        total = count = asset_count = source_bytes = 0
        if len(sources) > MAX_SOURCES:
            raise ImportError('too many asset sources')
        for source in map(Path, sources):
            if source.is_symlink():
                raise ImportError('source must not be a symbolic link: ' + str(source))
            source = source.resolve()
            entries = []
            if source.is_dir():
                root = source.parent if source.name == 'assets' else source
                for directory, dirs, files in os.walk(root, followlinks=False):
                    dirs.sort()
                    for name in dirs + files:
                        if (Path(directory) / name).is_symlink():
                            raise ImportError('symbolic links are not accepted: ' + str(Path(directory) / name))
                    for name in sorted(files):
                        path = Path(directory) / name
                        relative = safe_path(path.relative_to(root).as_posix())
                        count += 1
                        if count > MAX_FILES:
                            raise ImportError('too many source files')
                        if self.wanted(relative):
                            entries.append((relative, path, None, path.stat().st_size))
            elif source.is_file():
                info = source.stat()
                source_bytes += info.st_size
                if info.st_size > MAX_ARCHIVE_BYTES or source_bytes > MAX_SOURCE_BYTES:
                    raise ImportError('compressed source archive size limit exceeded')
                self.archive_stamps[source] = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
                with zipfile.ZipFile(source) as archive:
                    if len(archive.infolist()) > MAX_ARCHIVE_FILES:
                        raise ImportError('too many entries in one archive')
                    seen = set()
                    for entry in archive.infolist():
                        count += 1
                        if count > MAX_FILES:
                            raise ImportError('too many archive entries')
                        # Validate before ZipInfo separator normalization and NUL truncation.
                        name = safe_path(entry.orig_filename)
                        if stat.S_ISLNK(entry.external_attr >> 16):
                            raise ImportError('archive symbolic links are not accepted')
                        if not self.wanted(name) or entry.is_dir():
                            continue
                        if name in seen:
                            raise ImportError('duplicate archive entry: ' + name)
                        seen.add(name)
                        if entry.file_size > max(1024 * 1024, entry.compress_size * 1000):
                            raise ImportError('archive compression ratio limit exceeded')
                        entries.append((name, source, entry.filename, entry.file_size))
            else:
                raise ImportError('source does not exist: ' + str(source))
            for name, path, member, size in entries:
                asset_count += 1
                total += size
                if size > MAX_FILE or total > MAX_TOTAL:
                    raise ImportError('source asset size limit exceeded')
                if asset_count > MAX_ASSET_FILES:
                    raise ImportError('too many relevant asset files')
                self.files[name] = (path, member, size)
        if not any('/blockstates/' in key or '/dimension/' in key for key in self.files):
            raise ImportError('no blockstate assets or dimension definitions found')

    @staticmethod
    def wanted(name):
        return bool(re.fullmatch(r'assets/[^/]+/(?:blockstates|models)/.+\.json', name)
                    or re.fullmatch(r'assets/[^/]+/textures/.+\.png(?:\.mcmeta)?', name)
                    or re.fullmatch(r'data/[^/]+/(?:dimension|dimension_type)/.+\.json', name))

    def close(self):
        for archive in self.archives.values():
            archive.close()
        self.archives.clear()

    def __del__(self):
        self.close()

    def read(self, name):
        if name not in self.files:
            raise ImportError('missing asset: ' + name)
        path, member, size = self.files[name]
        if member is None:
            if path.is_symlink() or path.resolve() != path:
                raise ImportError('source changed to a symbolic link: ' + str(path))
            with path.open('rb') as stream:
                data = stream.read(MAX_FILE + 1)
        else:
            info = path.stat()
            if path.is_symlink() or path.resolve() != path or self.archive_stamps[path] != (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns):
                raise ImportError('source archive changed while importing')
            archive = self.archives.pop(path, None)
            if archive is None:
                if len(self.archives) >= 8:
                    _, old = self.archives.popitem(last=False)
                    old.close()
                archive = zipfile.ZipFile(path)
            self.archives[path] = archive
            entry = archive.getinfo(member)
            if entry.file_size != size:
                raise ImportError('source archive changed while importing')
            with archive.open(entry) as stream:
                data = stream.read(MAX_FILE + 1)
        if len(data) != size or len(data) > MAX_FILE:
            raise ImportError('asset size changed or exceeds limit')
        return data

    def json(self, path):
        try:
            value = json.loads(self.read(path), parse_constant=lambda s: (_ for _ in ()).throw(ImportError('nonfinite JSON number')))
        except (UnicodeError, ValueError, RecursionError) as error:
            raise ImportError(f'invalid JSON in {path}: {error}') from error
        if not isinstance(value, dict):
            raise ImportError('expected JSON object: ' + path)
        return value


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)


def encode_png(width, height, pixels):
    raw = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + png_chunk(b'IDAT', zlib.compress(raw, 9)) + png_chunk(b'IEND', b''))


def decode_png(data, on_trailing=None):
    """Bounded decoder for the common non-interlaced 8-bit Minecraft PNG formats."""
    if not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ImportError('invalid PNG signature')
    offset, compressed, palette, transparency = 8, bytearray(), b'', b''
    width = height = channels = color = 0
    ended = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ImportError('truncated PNG chunk')
        size, kind = struct.unpack('>I4s', data[offset:offset + 8])
        if size > MAX_FILE or offset + size + 12 > len(data):
            raise ImportError('invalid PNG chunk size')
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack('>I', data[offset + 8 + size:offset + 12 + size])[0]
        if binascii.crc32(kind + payload) & 0xffffffff != crc:
            raise ImportError('invalid PNG checksum')
        offset += size + 12
        if kind == b'IHDR':
            if width or size != 13:
                raise ImportError('invalid PNG header')
            width, height, depth, color, comp, filt, interlace = struct.unpack('>IIBBBBB', payload)
            channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color, 0)
            if not width or not height or width * height > MAX_PIXELS:
                raise ImportError('PNG dimensions exceed limit')
            if not channels or comp or filt or interlace or (depth != 8 and not (color in (0, 3) and depth in (1, 2, 4))):
                raise ImportError('PNG requires non-interlaced 8-bit or packed grayscale/indexed pixels')
        elif not width:
            raise ImportError('PNG IHDR must be first')
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'PLTE':
            palette = payload
        elif kind == b'tRNS':
            transparency = payload
        elif kind == b'IEND':
            ended = True
            break
    if not ended:
        raise ImportError('missing PNG end')
    if offset != len(data):
        if on_trailing is None:
            raise ImportError('trailing data after PNG end')
        on_trailing()
    stride = (width * channels * depth + 7) // 8
    pixel_bytes = max(1, (channels * depth + 7) // 8)
    expected = height * (1 + stride)
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(compressed, expected + 1)
    except zlib.error as error:
        raise ImportError('invalid PNG compression') from error
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ImportError('PNG decompressed size mismatch')
    output, previous = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ImportError('invalid PNG row filter')
        for i in range(stride):
            a = row[i - pixel_bytes] if i >= pixel_bytes else 0
            b = previous[i]
            c = previous[i - pixel_bytes] if i >= pixel_bytes else 0
            if filter_type == 1:
                row[i] = (row[i] + a) & 255
            elif filter_type == 2:
                row[i] = (row[i] + b) & 255
            elif filter_type == 3:
                row[i] = (row[i] + ((a + b) // 2)) & 255
            elif filter_type == 4:
                p = a + b - c
                distances = (abs(p - a), abs(p - b), abs(p - c))
                row[i] = (row[i] + (a, b, c)[distances.index(min(distances))]) & 255
        previous = row
        for x in range(width):
            i = x * channels
            px = row[i:i + channels] if depth == 8 else [(row[x * depth // 8] >> (8 - depth - (x * depth % 8))) & ((1 << depth) - 1)]
            if color == 6:
                output.extend(px)
            elif color == 2:
                alpha = 0 if len(transparency) == 6 and tuple(px) == struct.unpack('>3H', transparency) else 255
                output.extend((*px, alpha))
            elif color == 0:
                alpha = 0 if len(transparency) == 2 and px[0] == struct.unpack('>H', transparency)[0] else 255
                gray = px[0] * 255 // ((1 << depth) - 1)
                output.extend((gray, gray, gray, alpha))
            elif color == 4:
                output.extend((px[0], px[0], px[0], px[1]))
            else:
                index = px[0]
                if index * 3 + 3 > len(palette):
                    raise ImportError('invalid PNG palette index')
                output.extend((*palette[index * 3:index * 3 + 3], transparency[index] if index < len(transparency) else 255))
    return width, height, bytes(output)


def number(value, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ImportError(f'number outside {low}..{high}: {value!r}')
    return value


def vector(value, length, low, high):
    if not isinstance(value, list) or len(value) != length:
        raise ImportError(f'expected {length} coordinates')
    return [number(v, low, high) for v in value]


def properties(selector):
    result = {}
    if selector:
        for pair in selector.split(','):
            if pair.count('=') != 1:
                raise ImportError('invalid state selector: ' + selector)
            key, value = pair.split('=')
            if not PROPERTY.fullmatch(key) or not PROPERTY.fullmatch(value) or key in result:
                raise ImportError('invalid state property: ' + pair)
            result[key] = value
            if len(result) > 64:
                raise ImportError('state has more than 64 properties')
    return result


def canonical(name, values):
    result = name + ('[' + ','.join(f'{key}={values[key]}' for key in sorted(values)) + ']' if values else '')
    if len(result) > 1023 or len(values) > 64:
        raise ImportError('state selector exceeds runtime limit')
    return result


def default_uv(direction, start, end):
    x0, y0, z0 = start
    x1, y1, z1 = end
    return {
        'down': [x0, 16 - z1, x1, 16 - z0], 'up': [x0, z0, x1, z1],
        'north': [16 - x1, 16 - y1, 16 - x0, 16 - y0],
        'south': [x0, 16 - y1, x1, 16 - y0],
        'west': [z0, 16 - y1, z1, 16 - y0],
        'east': [16 - z1, 16 - y1, 16 - z0, 16 - y0],
    }[direction]


def cube_template(name):
    textures = None
    if name == 'minecraft:block/cube_all':
        textures = {side: '#all' for side in DIRECTIONS}
    elif name in ('minecraft:block/cube_column', 'minecraft:block/cube_bottom_top'):
        textures = {side: '#side' for side in DIRECTIONS}
        textures.update(down='#end' if name.endswith('column') else '#bottom', up='#end' if name.endswith('column') else '#top')
    elif name == 'minecraft:block/cube':
        textures = {side: '#' + side for side in DIRECTIONS}
    if textures is None:
        return None
    return {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16],
                          'faces': {side: {'texture': texture, 'cullface': side} for side, texture in textures.items()}}]}


def quarter_turn_orientation(x, y, z=0):
    """AE2's verified Quaternion.rotateYXZ(-y, -x, -z), as exact integer axes."""
    axes = []
    for point in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
        a, b, c = point
        for _ in range(z // 90):
            a, b = b, -a
        for _ in range(x // 90):
            b, c = c, -b
        for _ in range(y // 90):
            a, c = -c, a
        axes.append((a, b, c))
    return tuple(axes)


def cube_model(texture):
    return {'textures': {'all': texture}, **cube_template('minecraft:block/cube_all')}


class Compiler:
    def __init__(self, assets):
        self.assets = assets
        self.models = {}
        self.textures = {}
        self.warnings = set()
        self.element_count = 0
        self.unsupported_states = []

    def model(self, name, chain=()):
        inline = isinstance(name, dict)
        own = name if inline else None
        name = '<inline>' if inline else resource(name)
        if (not inline and name in chain) or len(chain) >= 64:
            raise ImportError('cyclic or excessive model parents: ' + name)
        if not inline and name in self.models:
            return self.models[name]
        if not inline:
            path = asset_path(name, 'models')
            own = self.assets.json(path) if path in self.assets.files else cube_template(name)
        if own is None:
            raise ImportError('missing model: ' + name)
        if 'loader' in own and own['loader'] not in ('neoforge:composite', 'forge:composite', 'fusion:model', 'lootr:brushable'):
            raise ImportError('custom model loader: ' + str(own['loader']))
        if own.get('loader') == 'fusion:model':
            if own.get('type', 'base') not in ('base', 'connecting', 'fusion:base', 'fusion:connecting'):
                raise ImportError('unsupported Fusion model type: ' + str(own.get('type')))
            self.warnings.add(f'{name}: connected-texture neighbors are not evaluated')
        parent = self.model(own['parent'], chain + (name,)) if 'parent' in own else {}
        result = {**parent, **own}
        if not isinstance(own.get('textures', {}), dict):
            raise ImportError('model textures must be an object: ' + name)
        result['textures'] = {**parent.get('textures', {}), **own.get('textures', {})}
        if not inline:
            self.models[name] = result
        return result

    def texture(self, name):
        name = resource(name)
        if name not in self.textures:
            path = asset_path(name, 'textures', '.png')
            if path not in self.assets.files:
                raise ImportError('missing texture: ' + name)
            width, height, pixels = decode_png(self.assets.read(path), lambda: self.warnings.add(f'{name}: discarded bytes after valid PNG end'))
            if path + '.mcmeta' in self.assets.files:
                metadata = self.assets.json(path + '.mcmeta')
                fusion = metadata.get('fusion', {})
                if fusion and not isinstance(fusion, dict):
                    raise ImportError('invalid Fusion texture metadata: ' + name)
                if fusion.get('type') in ('connecting', 'fusion:connecting'):
                    layout = fusion.get('layout', 'full')
                    grids = {'full': (8, 6), 'horizontal': (4, 1), 'vertical': (1, 4),
                             'simple': (4, 4), 'compact': (5, 1), 'pieced': (5, 1)}
                    if layout not in grids:
                        raise ImportError('unsupported Fusion texture layout: ' + str(layout))
                    cols, rows = grids[layout]
                    tile = width // cols
                    sheet_height = height
                    if layout == 'full' and width == height:
                        if 'animation' in metadata:
                            raise ImportError('padded full Fusion sheets cannot contain animation metadata: ' + name)
                        sheet_height = height * 6 // 8
                    if width % cols or not tile or sheet_height % (rows * tile):
                        raise ImportError('invalid Fusion texture sheet dimensions: ' + name)
                    pixels = b''.join(pixels[y * width * 4:(y * width + tile) * 4] for y in range(tile))
                    width = height = tile
                    self.warnings.add(f'{name}: connected-texture sheet uses its unconnected tile')
            if height != width:
                if height % width:
                    raise ImportError('texture must be square or a vertical animation: ' + name)
                self.warnings.add(f'{name}: animation uses the first frame')
                height, pixels = width, pixels[:width * width * 4]
            alpha_sum = sum(pixels[3::4])
            rgb = [sum(pixels[i] * pixels[i + 3 - c] for i in range(c, len(pixels), 4)) // max(1, alpha_sum) for c in range(3)]
            alpha = alpha_sum // (width * height)
            self.textures[name] = (encode_png(width, height, pixels), rgb, alpha)
        return name

    def overlay_texture(self, name, base, overlay):
        if name not in self.textures:
            self.texture(base)
            self.texture(overlay)
            bw, _, bottom = decode_png(self.textures[base][0])
            tw, _, top = decode_png(self.textures[overlay][0])
            width = max(bw, tw)
            pixels = bytearray()
            for y in range(width):
                for x in range(width):
                    bi = ((y * bw // width) * bw + x * bw // width) * 4
                    ti = ((y * tw // width) * tw + x * tw // width) * 4
                    ba, ta = bottom[bi + 3], top[ti + 3]
                    alpha = ta * 255 + ba * (255 - ta)
                    pixels.extend((top[ti + c] * ta * 255 + bottom[bi + c] * ba * (255 - ta)) // max(1, alpha) for c in range(3))
                    pixels.append((alpha + 127) // 255)
            alpha_sum = sum(pixels[3::4])
            rgb = [sum(pixels[i] * pixels[i + 3 - c] for i in range(c, len(pixels), 4)) // max(1, alpha_sum) for c in range(3)]
            self.textures[name] = (encode_png(width, width, pixels), rgb, alpha_sum // (width * width))
        return name

    def builtin_adapter(self, name, model):
        # These are narrow adapters for inspected AE2 19.2.18 built-in models.
        # Never replace a supplied standard model with its static Java fallback.
        if model != {'textures': {}}:
            return model
        if name == 'ae2:block/drive':
            self.warnings.add('ae2:block/drive: empty-drive asset geometry; installed cells and activity lights require block-entity data')
            return self.model('ae2:block/drive/drive_base')
        if name == 'ae2:block/quartz_glass':
            texture = self.overlay_texture('mineways:ae2/quartz_glass_isolated',
                                           'ae2:block/glass/quartz_glass_a', 'ae2:block/glass/quartz_glass_frame1111')
            self.warnings.add('ae2:block/quartz_glass: isolated glass frame with fixed pattern A; neighbor connections and random offsets are not evaluated')
            return cube_model(texture)
        if name == 'ae2:block/spatial_pylon':
            texture = self.overlay_texture('mineways:ae2/spatial_pylon_unlinked',
                                           'ae2:block/spatial_pylon/base', 'ae2:block/spatial_pylon/dim')
            self.warnings.add('ae2:block/spatial_pylon: unlinked, unpowered appearance; connection and activity data require block-entity data')
            return cube_model(texture)
        return model

    def fluid_definition(self, name, definition):
        # Inspected Allthemodium 3.2.1 FluidType client extensions supply these
        # actual still/flow textures. Particle textures in its JSON are generic.
        prefixes = {'allthemodium:molten_allthemodium_block': 'atm_molten',
                    'allthemodium:molten_vibranium_block': 'vibranium_molten',
                    'allthemodium:molten_unobtainium_block': 'unobtainium_molten',
                    'allthemodium:soul_lava_block': 'soul_lava'}
        prefix = prefixes.get(name)
        variants = definition.get('variants', {})
        if not prefix or not isinstance(variants, dict) or len(variants) != 1 or '' not in variants:
            return definition
        expected = 'allthemodium:block/molten' if prefix == 'soul_lava' else 'allthemodium:block/fluid/molten'
        if not isinstance(variants[''], dict) or variants[''].get('model') != expected:
            return definition
        original = self.model(expected)
        if original.get('elements') or original.get('loader'):
            return definition
        variants = {}
        for level in range(16):
            # LiquidBlock uses source amount8, flowing amounts7..1, falling amount8.
            # FlowingFluid.getOwnHeight is amount/9. Neighbor-height blending is omitted.
            height = (8 if level == 0 or level >= 8 else 8 - level) * 16 / 9
            faces = {side: {'texture': '#still' if side in ('up', 'down') else '#flow',
                            'uv': [0, 0, 16, 16] if side in ('up', 'down') else [0, (16 - height) / 2, 8, 8]}
                     for side in DIRECTIONS}
            model = {'textures': {'still': f'allthemodium:block/fluid/{prefix}_still',
                                  'flow': f'allthemodium:block/fluid/{prefix}_flow'},
                     'elements': [{'from': [0, 0, 0], 'to': [16, height, 16], 'faces': faces}]}
            variants[f'level={level}'] = {'model': model}
        variants[''] = variants['level=0']
        self.warnings.add(f'{name}: level-based flat fluid surface using actual still/flow textures; neighbor slopes, falling continuity, internal-face removal and light emission are not evaluated')
        return {'variants': variants}

    def missing_texture(self, alias):
        name = 'mineways:missing_texture'
        self.warnings.add(f'{alias}: unresolved texture alias uses the Minecraft missing-texture checkerboard')
        if name not in self.textures:
            pixels = bytes(channel for y in range(16) for x in range(16)
                           for channel in ((255, 0, 255, 255) if (x < 8) != (y < 8) else (0, 0, 0, 255)))
            self.textures[name] = (encode_png(16, 16, pixels), [127, 0, 127], 255)
        return name

    def compile_model(self, application, depth=0, selector=None):
        if depth >= 64:
            raise ImportError('excessive composite model depth')
        if isinstance(application, list):
            if not application:
                raise ImportError('empty model alternatives')
            self.warnings.add('Weighted model alternatives use the first listed model deterministically')
            application = application[0]
        if not isinstance(application, dict) or 'model' not in application:
            raise ImportError('invalid model application')
        x, y = application.get('x', 0), application.get('y', 0)
        if any(type(angle) is not int or not -(2 ** 31) <= angle < 2 ** 31 or angle % 90 for angle in (x, y)):
            raise ImportError('blockstate rotations must be signed 32-bit integer multiples of 90')
        x, y = x % 360, y % 360
        if 'ae2:z' in application:
            z = application['ae2:z']
            if type(z) is not int or z not in (0, 90, 180, 270):
                raise ImportError('invalid AE2 Z rotation')
            orientation = quarter_turn_orientation(x, y, z)
            equivalent = next(((a, b) for a in (0, 90, 180, 270) for b in (0, 90, 180, 270)
                               if quarter_turn_orientation(a, b) == orientation), None)
            if equivalent is None:
                raise ImportError('AE2 Z rotation cannot be represented by supported X/Y model rotations')
            x, y = equivalent
        uvlock = application.get('uvlock', False)
        if not isinstance(uvlock, bool):
            raise ImportError('uvlock must be boolean')
        model = self.model(application['model'])
        name = application['model']
        if name == 'ae2:block/matrix_frame' and model == {'textures': {}}:
            return {'rotation': (x, y, int(uvlock)), 'elements': [], 'invisible': True}
        model = self.builtin_adapter(name, model)
        if model.get('loader') == 'lootr:brushable':
            stage = (selector or {}).get('dusted')
            if stage not in ('0', '1', '2', '3'):
                raise ImportError('Lootr brushable model requires an explicit dusted=0..3 selector')
            child = model.get('stage_' + stage)
            if not isinstance(child, dict):
                raise ImportError('missing Lootr brushable stage model: ' + stage)
            self.warnings.add(f'{application["model"]}: Lootr uses dusted stage without player-opened appearance; player data is not in the block palette')
            return self.compile_model({'model': child, 'x': x, 'y': y, 'uvlock': uvlock}, depth + 1, selector)
        if model.get('loader') in ('neoforge:composite', 'forge:composite'):
            if any(key in model for key in ('transform', 'visibility')):
                raise ImportError('composite root transforms and visibility require explicit replacement models')
            children = model.get('children')
            if not isinstance(children, dict) or not children or len(children) > 256:
                raise ImportError('invalid or excessive composite children')
            if not all(isinstance(child, dict) for child in children.values()):
                raise ImportError('composite child must be an inline model object')
            elements = []
            for child in children.values():
                elements.extend(self.compile_model({'model': child}, depth + 1, selector)['elements'])
            if len(elements) > MAX_ELEMENTS:
                raise ImportError('composite element count exceeds limit')
            return {'rotation': (x, y, int(uvlock)), 'elements': elements}
        elements = model.get('elements')
        if not isinstance(elements, list) or not elements or len(elements) > MAX_ELEMENTS:
            raise ImportError('model needs 1..2048 standard elements: ' + str(application['model']))
        self.element_count += len(elements)
        if self.element_count > MAX_COMPILED_ELEMENTS:
            raise ImportError('compiled element budget exceeded')
        result = {'rotation': (x, y, int(uvlock)), 'elements': []}
        for element in elements:
            if not isinstance(element, dict):
                raise ImportError('invalid model element')
            start = vector(element.get('from'), 3, -16, 32)
            end = vector(element.get('to'), 3, -16, 32)
            if any(a > b for a, b in zip(start, end)):
                raise ImportError('inverted elements require an explicit replacement model')
            rotation = element.get('rotation', {})
            if not isinstance(rotation, dict):
                raise ImportError('invalid element rotation')
            axis = rotation.get('axis', 'none')
            angle = number(rotation.get('angle', 0), -45, 45)
            if axis not in ('none', 'x', 'y', 'z') or (axis == 'none' and angle != 0) or any(key in rotation for key in ('x', 'y', 'z')):
                raise ImportError('unsupported element rotation')
            origin = vector(rotation.get('origin', [8, 8, 8]), 3, -16, 32)
            rescale = rotation.get('rescale', False)
            if not isinstance(rescale, bool):
                raise ImportError('rescale must be boolean')
            compiled = {'coordinates': (*start, *end, axis, angle, *origin, int(rescale)), 'faces': []}
            faces = element.get('faces', {})
            if not isinstance(faces, dict) or not faces:
                raise ImportError('element has no faces')
            for side, face in sorted(faces.items()):
                if side not in DIRECTIONS or not isinstance(face, dict):
                    raise ImportError('invalid element face')
                texture = face.get('texture', '')
                seen = set()
                while isinstance(texture, str) and texture.startswith('#'):
                    if texture in seen or len(seen) >= 64:
                        raise ImportError('cyclic texture references')
                    seen.add(texture)
                    if texture[1:] not in model['textures']:
                        texture = self.missing_texture(str(application['model']) + ':' + texture)
                        break
                    texture = model['textures'][texture[1:]]
                texture = self.texture(texture)
                uv = vector(face.get('uv', default_uv(side, start, end)), 4, 0, 16)
                turn = face.get('rotation', 0)
                if type(turn) is not int or turn not in (0, 90, 180, 270):
                    raise ImportError('invalid face rotation')
                cull = face.get('cullface', '-')
                if cull not in (*DIRECTIONS, '-'):
                    raise ImportError('invalid cullface')
                if face.get('tintindex', -1) != -1:
                    self.warnings.add(f'{application["model"]}: biome/custom tint is not applied')
                compiled['faces'].append((side, *uv, turn, texture, cull))
            result['elements'].append(compiled)
        return result

    def block(self, name, definition):
        definition = self.fluid_definition(name, definition)
        selectors = []
        if 'variants' in definition:
            variants = definition['variants']
            if not isinstance(variants, dict) or not variants or len(variants) > MAX_STATES:
                raise ImportError('invalid or excessive block variants')
            selectors = [(properties(key), [value]) for key, value in sorted(variants.items())]
        elif 'multipart' in definition:
            parts = definition['multipart']
            if not isinstance(parts, list) or not parts or len(parts) > MAX_ELEMENTS:
                raise ImportError('invalid multipart blockstate')
            domains = {}
            def collect(condition):
                if not isinstance(condition, dict):
                    raise ImportError('invalid multipart condition')
                for key, value in condition.items():
                    if key in ('OR', 'AND'):
                        if not isinstance(value, list) or not value:
                            raise ImportError('invalid multipart logical condition')
                        for child in value:
                            collect(child)
                    else:
                        if isinstance(value, bool):
                            value = 'true' if value else 'false'
                            condition[key] = value
                        if not PROPERTY.fullmatch(key) or not isinstance(value, str):
                            raise ImportError('invalid multipart property')
                        values = value.split('|')
                        if not all(PROPERTY.fullmatch(v) for v in values):
                            raise ImportError('invalid multipart property values')
                        domains.setdefault(key, set()).update(values)
                        if set(values) <= {'true', 'false'}:
                            domains[key].update(('true', 'false'))
            for part in parts:
                if not isinstance(part, dict) or 'apply' not in part:
                    raise ImportError('invalid multipart entry')
                collect(part.get('when', {}))
            unconditional = [part['apply'] for part in parts if not part.get('when')]
            if unconditional and domains:
                selectors.append(({}, unconditional))
            if math.prod(map(len, domains.values())) > MAX_STATES:
                raise ImportError('multipart expansion exceeds 4096 selectors')
            def matches(condition, state):
                return all((any(matches(c, state) for c in value) if key == 'OR' else
                            all(matches(c, state) for c in value) if key == 'AND' else
                            state.get(key) in value.split('|')) for key, value in condition.items())
            keys = sorted(domains)
            for values in itertools.product(*(sorted(domains[key]) for key in keys)):
                state = dict(zip(keys, values))
                applications = [p['apply'] for p in parts if matches(p.get('when', {}), state)]
                if applications:
                    selectors.append((state, applications))
        else:
            raise ImportError('blockstate needs variants or multipart')
        output = []
        failures = []
        for selector, applications in selectors:
            before_count, before_textures = self.element_count, len(self.textures)
            before_warnings = self.warnings.copy()
            try:
                if len(applications) > 256:
                    raise ImportError('state has more than 256 model applications')
                models = [self.compile_model(value, selector=selector) for value in applications]
                if models and all(model.get('invisible') for model in models):
                    output.append((canonical(name, selector), [0, 0, 0], 0, []))
                    continue
                faces = [face for model in models for element in model['elements'] for face in element['faces']]
                if not faces:
                    raise ImportError('state has no standard model faces')
                face = next((face for face in faces if face[0] == 'up'), faces[0])
                _, color, alpha = self.textures[face[-2]]
                output.append((canonical(name, selector), color, alpha, models))
            except (ImportError, RecursionError) as error:
                self.element_count = before_count
                while len(self.textures) > before_textures:
                    self.textures.popitem()
                self.warnings = before_warnings
                failures.append({'state': canonical(name, selector), 'reason': str(error)})
        if failures:
            bad_properties = [properties(item['state'].partition('[')[2][:-1]) for item in failures]
            safe_output = []
            for row in output:
                good_properties = properties(row[0].partition('[')[2][:-1])
                if any(all(bad.get(key) == value for key, value in good_properties.items()) for bad in bad_properties):
                    failures.append({'state': row[0], 'reason': 'generic selector overlaps an unsupported state; fallback removed'})
                else:
                    safe_output.append(row)
            output = safe_output
        if not output:
            raise ImportError(failures[0]['reason'] if failures else 'block has no supported states')
        self.unsupported_states.extend(failures)
        return output


def compile_dimensions(assets, namespaces):
    dimensions, unsupported = [], []
    for path in sorted(assets.files):
        match = re.fullmatch(r'data/([a-z0-9_.-]+)/dimension/(.+)\.json', path)
        if not match or (namespaces and match[1] not in namespaces) or (not namespaces and match[1] == 'minecraft'):
            continue
        name = resource(match[1] + ':' + match[2])
        try:
            if len(name) > 255 or len(dimensions) >= 256:
                raise ImportError('dimension identifier or dimension count exceeds runtime limit')
            definition = assets.json(path)
            kind = definition.get('type')
            if isinstance(kind, str):
                namespace, entry = resource(kind).split(':')
                kind = assets.json(f'data/{namespace}/dimension_type/{entry}.json')
            if not isinstance(kind, dict):
                raise ImportError('dimension has no complete dimension type')
            minimum, height = kind.get('min_y'), kind.get('height')
            if type(minimum) is not int or type(height) is not int or minimum % 16 or height % 16:
                raise ImportError('dimension min_y and height must be integer multiples of 16')
            if minimum < -2032 or height < 16 or height > 4064 or minimum + height > 2032:
                raise ImportError('dimension height lies outside -2032..2031')
            scale = number(kind.get('coordinate_scale'), 0.000001, 1000000)
            dimensions.append((name, minimum, height, scale))
        except (ImportError, RecursionError) as error:
            unsupported.append({'dimension': name, 'reason': str(error)})
    return dimensions, unsupported


def compile_pack(sources, destination, namespaces=None):
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise ImportError('output already exists: ' + str(destination))
    if not destination.parent.is_dir():
        raise ImportError('output parent directory does not exist')
    if namespaces and 'minecraft' in namespaces:
        raise ImportError('minecraft block replacements are not supported; use vanilla assets as parent/texture sources')
    assets = Assets(sources)
    dimensions, unsupported_dimensions = compile_dimensions(assets, namespaces)
    compiler = Compiler(assets)
    rejected, texture_ids, geometry_ids = [], {}, {}
    block_count = selector_count = element_count = 0
    temporary = Path(tempfile.mkdtemp(prefix='.mineways-pack-', dir=destination.parent))
    try:
        (temporary / 'textures').mkdir()
        with (temporary / 'geometry.tmp').open('w', encoding='utf-8', newline='\n') as geometry_stream, \
             (temporary / 'states.tmp').open('w', encoding='utf-8', newline='\n') as state_stream:
            for path in sorted(assets.files):
                match = re.fullmatch(r'assets/([a-z0-9_.-]+)/blockstates/(.+)\.json', path)
                if not match or (namespaces and match[1] not in namespaces) or (not namespaces and match[1] == 'minecraft'):
                    continue
                name = resource(match[1] + ':' + match[2])
                compiler.element_count = 0
                before_textures, before_states = len(compiler.textures), len(compiler.unsupported_states)
                before_warnings = compiler.warnings.copy()
                try:
                    block = compiler.block(name, assets.json(path))
                except (ImportError, RecursionError) as error:
                    while len(compiler.textures) > before_textures:
                        compiler.textures.popitem()
                    del compiler.unsupported_states[before_states:]
                    compiler.warnings = before_warnings
                    rejected.append({'block': name, 'reason': str(error)})
                    continue
                if selector_count + len(block) > MAX_CATALOG_SELECTORS or block_count >= MAX_CATALOG_BLOCKS:
                    raise ImportError('compiled block or selector catalog exceeds runtime limit')
                block_count += 1
                selector_count += len(block)
                for selector, color, alpha, models in block:
                    state_stream.write(f'state\t{selector}\t' + ''.join(f'{c:02x}' for c in color) + f'\t{alpha}\n')
                    if not models:
                        state_stream.write('invisible\n')
                    for model in models:
                        signature = hashlib.sha256(json.dumps(model['elements'], sort_keys=True, separators=(',', ':')).encode()).digest()
                        index = geometry_ids.get(signature)
                        if index is None:
                            index = len(geometry_ids)
                            element_count += len(model['elements'])
                            if index >= MAX_GEOMETRIES or element_count > MAX_COMPILED_ELEMENTS:
                                raise ImportError('compiled shared geometry exceeds runtime limit')
                            geometry_ids[signature] = index
                            geometry_stream.write(f'geometry\t{index}\n')
                            for element in model['elements']:
                                geometry_stream.write('element\t' + '\t'.join(map(str, element['coordinates'])) + '\n')
                                for face in element['faces']:
                                    texture = face[-2]
                                    if texture not in texture_ids:
                                        if len(texture_ids) >= MAX_TEXTURES:
                                            raise ImportError('compiled texture catalog exceeds runtime limit')
                                        texture_ids[texture] = len(texture_ids)
                                    fields = (*face[:-2], texture_ids[texture], face[-1])
                                    geometry_stream.write('face\t' + '\t'.join(map(str, fields)) + '\n')
                            geometry_stream.write('endgeometry\n')
                        state_stream.write('usemodel\t' + '\t'.join(map(str, (index, *model['rotation']))) + '\n')
                    state_stream.write('endstate\n')
                if geometry_stream.tell() + state_stream.tell() > MAX_MANIFEST:
                    raise ImportError('compiled manifest exceeds runtime size limit')
        if not block_count and not dimensions:
            raise ImportError('no supported mod blocks or dimensions found' + (': ' + rejected[0]['reason'] if rejected else ''))
        report = {'format': 2, 'blocks': block_count, 'selectors': selector_count,
                  'textures': len(texture_ids), 'geometries': len(geometry_ids), 'elements': element_count,
                  'dimensions': len(dimensions), 'unsupported_dimensions': unsupported_dimensions,
                  'unsupported': rejected, 'unsupported_states': compiler.unsupported_states,
                  'warnings': sorted(compiler.warnings)}
        with (temporary / 'manifest.mwpack').open('w', encoding='utf-8', newline='\n') as manifest:
            manifest.write('MINEWAYS_MOD_PACK\t2\n')
            for dimension in dimensions:
                manifest.write('dimension\t' + '\t'.join(map(str, dimension)) + '\n')
            for name, index in texture_ids.items():
                manifest.write(f'texture\t{index}\t{name}\ttextures/{index:04}.png\n')
                (temporary / 'textures' / f'{index:04}.png').write_bytes(compiler.textures[name][0])
            for spool in ('geometry.tmp', 'states.tmp'):
                with (temporary / spool).open(encoding='utf-8') as source:
                    shutil.copyfileobj(source, manifest)
                (temporary / spool).unlink()
        if (temporary / 'manifest.mwpack').stat().st_size > MAX_MANIFEST:
            raise ImportError('compiled manifest exceeds runtime size limit')
        (temporary / 'report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        temporary.rename(destination)
    finally:
        assets.close()
        if temporary.exists():
            shutil.rmtree(temporary)
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='*', help='asset directories or JAR/ZIP archives, later sources override earlier sources')
    parser.add_argument('--sources-file', help='UTF-8 file with one unquoted source path per line; these sources precede positional sources')
    parser.add_argument('-o', '--output', required=True, help='new output directory (existing output is never overwritten)')
    parser.add_argument('--namespace', action='append', help='only import this namespace (repeatable, default: all non-minecraft namespaces)')
    args = parser.parse_args()
    try:
        sources = []
        if args.sources_file:
            with Path(args.sources_file).open('rb') as source_file:
                data = source_file.read(1024 * 1024 + 1)
            if len(data) > 1024 * 1024:
                raise ImportError('source list exceeds 1 MiB')
            sources = [line for line in data.decode('utf-8-sig').splitlines() if line.strip()]
        sources.extend(args.sources)
        if not sources:
            parser.error('provide a source path or a nonempty --sources-file')
        report = compile_pack(sources, args.output, args.namespace)
    except (ImportError, OSError, UnicodeError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, f'Import failed: {error}\n')
    print(f'Created {args.output}: {report["blocks"]} blocks, {report["selectors"]} selectors, {report["textures"]} textures, {report["dimensions"]} dimensions')
    print(f'{len(report["unsupported"])} unsupported blocks; {len(report["unsupported_states"])} unsupported states; {len(report["unsupported_dimensions"])} incomplete dimensions; {len(report["warnings"])} warnings. Read report.json before use.')


if __name__ == '__main__':
    main()
