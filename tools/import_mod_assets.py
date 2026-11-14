#!/usr/bin/env python3
"""Compile Minecraft asset folders/JARs/ZIPs to a data-only Mineways mod pack."""

import argparse
import binascii
from collections import OrderedDict
import itertools
import importlib.util
from types import SimpleNamespace
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

def _load_helper(name):
    spec = importlib.util.spec_from_file_location('mineways_' + name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_obj_module = _load_helper('mod_obj')
_ie_module = _load_helper('mod_ie_models')
_conveyor_module = _load_helper('mod_conveyor_models')
_faucet_module = _load_helper('mod_faucet_models')
_canvas_sign_module = _load_helper('mod_canvas_sign_models')
_twilight_sign_module = _load_helper('mod_twilight_sign_models')
_split_module = _load_helper('mod_ie_split')
_machine_module = _load_helper('mod_machine_models')
_storage_module = _load_helper('mod_storage_models')
_material_module = _load_helper('mod_material_models')
_mekanism_module = _load_helper('mod_mekanism_models')
_enderio_module = _load_helper('mod_enderio_models')
_wire_module = _load_helper('mod_wire_models')
_framework_module = _load_helper('mod_framework_models')
_custom_defaults = _load_helper('mod_custom_defaults')
_lantern_module = _load_helper('mod_lantern_models')
_ie_sides_module = _load_helper('mod_ie_sides')
_power_module = _load_helper('mod_power_models')
_backpack_module = _load_helper('mod_backpack_models')
_shulker_module = _load_helper('mod_shulker_models')
_chest_module = _load_helper('mod_chest_models')
_xycraft_pipe_module = _load_helper('mod_xycraft_pipe_models')
_pressure_tube_module = _load_helper('mod_pressure_tube_models')
_twilight_module = _load_helper('mod_twilight_models')
_cloudfx_module = _load_helper('mod_cloudfx')

# Custom geometry is parsed where a loader is declared. Child baking context
# (textures, visibility, transforms) remains overridable independently.
OBJ_GEOMETRY_FIELDS = ('model', 'automatic_culling', 'shade_quads', 'flip_v', 'emissive_ambient', 'mtl_override')
LOADER_GEOMETRY_FIELDS = {
    'neoforge:composite': ('children', 'item_render_order'),
    'forge:composite': ('children', 'item_render_order'),
    'neoforge:obj': OBJ_GEOMETRY_FIELDS,
    _mekanism_module.LOADER: OBJ_GEOMETRY_FIELDS + ('glass',),
    _mekanism_module.DATA_LOADER: ('no_data', 'emitting'),
    'immersiveengineering:ie_obj': ('model', 'callbacks', 'dynamic', 'layers'),
    _conveyor_module.LOADER: ('conveyorType',),
    _faucet_module.LOADER: ('model', 'liquid'),
    _framework_module.LOADER: ('components',),
    _custom_defaults.FRAMED_LOADER: ('children', 'parts', 'item_render_order'),
    _lantern_module.LOADER: ('support',),
    _ie_sides_module.LOADER: ('base_name', 'type'),
    _power_module.PIPE_LOADER: ('pipeType', 'transparent'),
    _pressure_tube_module.LOADER: ('prefix',),
    _xycraft_pipe_module.LOADER: _xycraft_pipe_module.FIELDS,
    _power_module.CUBE_LOADER: _power_module.CUBE_FIELDS,
    _twilight_module.CONNECTED: ('base', 'connected_texture', 'connectable_blocks'),
    _twilight_module.FORCE_FIELD: ('elements',),
    _storage_module.SIMPLE_MATERIAL_LOADER: ('base', 'overlay'),
    _wire_module.WIRE_LOADER: ('line', 'edge'),
    _wire_module.TINT_LOADER: ('elements',),
    'modern_industrialization:machine': ('casing', 'default_overlays', 'tiered_overlays', 'no_overlay_on_output_side'),
    'modern_industrialization:use_block_model': ('block',),
}

PALETTE_FLAGS = {
    _storage_module.BARREL_LOADER: ('open',),
    _backpack_module.LOADER: ('left_tank', 'right_tank', 'battery'),
    _twilight_module.FORCE_FIELD: _twilight_module.DIRECTIONS,
}

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
MIN_MODEL_COORDINATE = -256
MAX_MODEL_COORDINATE = 256
MAX_PIXELS = 4 * 1024 * 1024
MAX_COMPILED_ELEMENTS = 4000000
MIN_MODEL_UV = -256
MAX_MODEL_UV = 256
MAX_UV_CELLS = 256
REPEATED_UV_WARNING = (
    "out-of-range UVs repeat the source texture; this approximates Minecraft atlas "
    "extrapolation because neighboring atlas sprites are unavailable")
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


class NoModDefinitions(ImportError):
    """All indexed block and dimension definitions belong to vanilla Minecraft."""


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
        self.atlas_sources = []
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
                        if stat.S_ISLNK(entry.external_attr >> 16):
                            raise ImportError('archive symbolic links are not accepted')
                        # A root label such as "LICENSE_Create: Addon Compatibility" is
                        # an opaque ZIP key. It cannot supply assets and is never extracted.
                        raw = entry.orig_filename
                        if ':' in raw and not any(ch in raw for ch in '/\\\0') and not re.match(r'^[A-Za-z]:', raw):
                            continue
                        name = safe_path(raw)
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
                if name == 'assets/minecraft/atlases/blocks.json':
                    self.atlas_sources.append((path, member, size))
        if not any('/blockstates/' in key or '/dimension/' in key for key in self.files):
            raise ImportError('no blockstate assets or dimension definitions found')

    @staticmethod
    def wanted(name):
        return bool(name == 'assets/minecraft/atlases/blocks.json'
                    or re.fullmatch(r'assets/[^/]+/(?:blockstates|models|storage_texture_definitions)/.+\.json', name)
                    or re.fullmatch(r'assets/[^/]+/models/.+\.(?:obj(?:\.(?:ie|mek))?|mtl)', name)
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
        return self.read_entry(self.files[name])

    def read_entry(self, entry):
        path, member, size = entry
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

    def json(self, path, entry=None):
        try:
            value = json.loads(self.read(path) if entry is None else self.read_entry(entry), parse_constant=lambda s: (_ for _ in ()).throw(ImportError('nonfinite JSON number')))
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
    """Bounded PNG decoder, including Adam7 and 16-bit samples, returning RGBA8."""
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
            depths = {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}
            if depth not in depths.get(color, ()) or comp or filt or interlace not in (0, 1):
                raise ImportError('invalid PNG color type, depth, compression, filter, or interlace method')
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
    # Adam7 scanlines are filtered independently within each nonempty pass.
    # Compute the exact decompressed bound before allocating or inflating data.
    steps = ((0, 0, 1, 1),) if not interlace else (
        (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
        (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
    passes = []
    for x0, y0, dx, dy in steps:
        pass_width = max(0, (width - x0 + dx - 1) // dx)
        pass_height = max(0, (height - y0 + dy - 1) // dy)
        if pass_width and pass_height:
            stride = (pass_width * channels * depth + 7) // 8
            passes.append((x0, y0, dx, dy, pass_width, pass_height, stride))
    pixel_bytes = max(1, (channels * depth + 7) // 8)
    expected = sum(pass_height * (1 + stride) for _, _, _, _, _, pass_height, stride in passes)
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(compressed, expected + 1)
    except zlib.error as error:
        raise ImportError('invalid PNG compression') from error
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ImportError('PNG decompressed size mismatch')
    transparent_rgb = struct.unpack('>3H', transparency) if color == 2 and len(transparency) == 6 else None
    transparent_gray = struct.unpack('>H', transparency)[0] if color == 0 and len(transparency) == 2 else None
    output, cursor = bytearray(width * height * 4), 0
    for x0, y0, dx, dy, pass_width, pass_height, stride in passes:
        previous = bytearray(stride)
        for y in range(pass_height):
            filter_type = raw[cursor]
            row = bytearray(raw[cursor + 1:cursor + 1 + stride])
            cursor += stride + 1
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
            rgba = bytearray()
            for x in range(pass_width):
                i = x * channels
                if depth == 16:
                    samples = struct.unpack_from('>' + 'H' * channels, row, i * 2)
                    # Match Mineways' native PNG decoder: retain the high byte.
                    px = [value >> 8 for value in samples]
                elif depth == 8:
                    samples = px = row[i:i + channels]
                else:
                    samples = px = [(row[x * depth // 8] >> (8 - depth - (x * depth % 8))) & ((1 << depth) - 1)]
                if color == 6:
                    rgba.extend(px)
                elif color == 2:
                    alpha = 0 if tuple(samples) == transparent_rgb else 255
                    rgba.extend((*px, alpha))
                elif color == 0:
                    alpha = 0 if samples[0] == transparent_gray else 255
                    gray = px[0] if depth == 16 else px[0] * 255 // ((1 << depth) - 1)
                    rgba.extend((gray, gray, gray, alpha))
                elif color == 4:
                    rgba.extend((px[0], px[0], px[0], px[1]))
                else:
                    index = px[0]
                    if index * 3 + 3 > len(palette):
                        raise ImportError('invalid PNG palette index')
                    rgba.extend((*palette[index * 3:index * 3 + 3], transparency[index] if index < len(transparency) else 255))
            destination = ((y0 + y * dy) * width + x0) * 4
            if dx == 1:
                output[destination:destination + len(rgba)] = rgba
            else:
                for x in range(pass_width):
                    index = destination + x * dx * 4
                    output[index:index + 4] = rgba[x * 4:x * 4 + 4]

    return width, height, bytes(output)


def number(value, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high or not math.isfinite(value):
        raise ImportError(f'number outside {low}..{high}: {value!r}')
    return value


def vector(value, length, low, high):
    if not isinstance(value, list) or len(value) != length:
        raise ImportError(f'expected {length} coordinates')
    return [number(v, low, high) for v in value]



def expanded_uv(points):
    """Validate source UVs and bound the native repeated-texture subdivision work."""
    us = [number(point[0], MIN_MODEL_UV, MAX_MODEL_UV) for point in points]
    vs = [number(point[1], MIN_MODEL_UV, MAX_MODEL_UV) for point in points]
    expanded = any(value < 0 or value > 16 for value in (*us, *vs))
    if expanded:
        cells = math.prod(max(1, math.ceil(max(axis) / 16) - math.floor(min(axis) / 16))
                          if min(axis) != max(axis) else 1 for axis in (us, vs))
        if cells > MAX_UV_CELLS:
            raise ImportError('UV subdivision exceeds 256 texture cells per face')
    return expanded


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
        self.face_texture_pixels = {}
        self.face_texture_pixel_count = 0
        self.warnings = set()
        self.element_count = 0
        self.unsupported_states = []
        self.cloudfx_declarations = None
        self.cloudfx_frames = {}

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
        # Only the absent engine parent can create this trusted placeholder marker.
        if own is not None and '_builtin_entity' in own:
            own = {key: value for key, value in own.items() if key != '_builtin_entity'}
        if own is None:
            if name == 'minecraft:builtin/entity':
                own = {'_builtin_entity': True}
            else:
                raise ImportError('missing model: ' + name)
        if 'loader' in own and own['loader'] not in ('neoforge:composite', 'forge:composite', 'fusion:model', 'lootr:brushable',
                                                        'refinedstorage:cable', 'bibliocraft:bookcase',
                                                        'bibliocraft:table', 'xycraft_core:connected_textures',
                                                        'neoforge:obj', 'immersiveengineering:ie_obj', 'immersiveengineering:basic_split', 'supplementaries:extra_rotation',
                                                        _storage_module.SIMPLE_MATERIAL_LOADER, _storage_module.LIMITED_BARREL_LOADER, _storage_module.BARREL_LOADER,
                                                        _material_module.DOMUM_LOADER, _mekanism_module.LOADER, _mekanism_module.DATA_LOADER, _storage_module.PORTABLE_LOADER, _enderio_module.IO_OVERLAY_LOADER,
                                                        _framework_module.LOADER, *_wire_module.LOADERS,
                                                        _custom_defaults.FRAMED_LOADER, _custom_defaults.MICROCHIP_LOADER,
                                                        _lantern_module.LOADER, _faucet_module.LOADER, _ie_sides_module.LOADER, _conveyor_module.LOADER, *_power_module.LOADERS,
                                                        _backpack_module.LOADER, _shulker_module.LOADER, _chest_module.LOADER, _xycraft_pipe_module.LOADER, _pressure_tube_module.LOADER, *_twilight_module.LOADERS,
                                                        *_machine_module.LOADERS):
            raise ImportError('custom model loader: ' + str(own['loader']))
        if own.get('loader') == 'fusion:model':
            if own.get('type', 'base') not in ('base', 'connecting', 'fusion:base', 'fusion:connecting'):
                raise ImportError('unsupported Fusion model type: ' + str(own.get('type')))
        parent = self.model(own['parent'], chain + (name,)) if 'parent' in own else {}
        result = {**parent, **own}
        source = own if 'loader' in own else parent
        for field in LOADER_GEOMETRY_FIELDS.get(result.get('loader'), ()):
            if field in source:
                result[field] = source[field]
            else:
                result.pop(field, None)
        if result.get('loader') == _material_module.DOMUM_LOADER:
            result['_domum_geometry_parent'] = (own.get('parent') if 'loader' in own
                                                else parent.get('_domum_geometry_parent'))
        if not isinstance(own.get('textures', {}), dict):
            raise ImportError('model textures must be an object: ' + name)
        result['textures'] = {**parent.get('textures', {}), **own.get('textures', {})}
        if 'visibility' in own or 'visibility' in parent:
            if not isinstance(own.get('visibility', {}), dict) or not isinstance(parent.get('visibility', {}), dict):
                raise ImportError('model visibility must be an object')
            result['visibility'] = {**parent.get('visibility', {}), **own.get('visibility', {})}
        if not inline:
            self.models[name] = result
        return result

    def texture(self, name):
        name = resource(name)
        if name not in self.textures:
            path = asset_path(name, 'textures', '.png')
            generated = path not in self.assets.files
            if generated:
                if name not in _cloudfx_module.CLOUD_IDS:
                    raise ImportError('missing texture: ' + name)
                if self.cloudfx_declarations is None:
                    self.cloudfx_declarations = _cloudfx_module.declarations(self.assets, resource, asset_path, ImportError)
                declaration = self.cloudfx_declarations.get(name)
                if declaration is None:
                    raise ImportError('missing CloudFX atlas declaration: ' + name)
                if 'resource' in declaration:
                    original = self.texture(declaration['resource'])
                    self.textures[name] = self.textures[original]
                    return name
                if name not in self.cloudfx_frames:
                    self.cloudfx_frames[name] = _cloudfx_module.generate(declaration, ImportError)
                width, height, pixels = self.cloudfx_frames[name]
                self.warnings.add(f'{name}: generated XyCraft CloudFX with fixed seed 0 and warmed frame 129; animation is frozen and the live clock-based seed is unavailable')
            else:
                width, height, pixels = decode_png(self.assets.read(path), lambda: self.warnings.add(f'{name}: discarded bytes after valid PNG end'))
            metadata = {}
            if not generated and path + '.mcmeta' in self.assets.files:
                metadata = self.assets.json(path + '.mcmeta')
                fusion = metadata.get('fusion', {})
                if not isinstance(fusion, dict):
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
            if 'animation' in metadata and fusion.get('type') not in ('connecting', 'fusion:connecting'):
                animation = metadata['animation']
                if not isinstance(animation, dict):
                    raise ImportError('invalid animation metadata: ' + name)
                frame_width, frame_height = animation.get('width', -1), animation.get('height', -1)
                if any(type(n) is not int or (n != -1 and not 1 <= n <= 2147483647)
                       for n in (frame_width, frame_height)):
                    raise ImportError('invalid animation frame dimensions: ' + name)
                if frame_width == -1 and frame_height == -1:
                    frame_width = frame_height = min(width, height)
                else:
                    frame_width = width if frame_width == -1 else frame_width
                    frame_height = height if frame_height == -1 else frame_height
                if width % frame_width or height % frame_height:
                    raise ImportError('invalid animation frame dimensions: ' + name)
                def duration_ticks(value):
                    # GsonHelper accepts numeric primitives and truncates fractional
                    # durations toward zero. Keep bounds instead of Java overflow.
                    if type(value) not in (int, float) or not 1 <= value < 2147483648:
                        raise ImportError('invalid animation frame duration: ' + name)
                    return int(value)
                frame_time = duration_ticks(animation.get('frametime', 1))
                frames = animation.get('frames', [])
                if not isinstance(frames, list):
                    raise ImportError('invalid animation frames: ' + name)
                columns = width // frame_width
                frame_count = columns * (height // frame_height)
                indices = []
                for frame in frames:
                    index = frame.get('index') if isinstance(frame, dict) else frame
                    duration = frame.get('time', frame_time) if isinstance(frame, dict) else frame_time
                    if type(index) is not int or not 0 <= index < frame_count:
                        raise ImportError('invalid animation frame index: ' + name)
                    duration_ticks(duration)
                    indices.append(index)
                # Minecraft 1.21.1 creates no animation for a singleton sequence,
                # then uploads the frame at the image origin, regardless of its index.
                first = indices[0] if len(indices) > 1 else 0
                left, top = first % columns * frame_width, first // columns * frame_height
                pixels = b''.join(pixels[((top + y) * width + left) * 4:((top + y) * width + left + frame_width) * 4]
                                  for y in range(frame_height))
                width, height = frame_width, frame_height
                if len(indices) == 1:
                    self.warnings.add(f'{name}: singleton animation uses the image origin, matching Minecraft 1.21.1')
                else:
                    self.warnings.add(f'{name}: animation uses the first frame in its sequence')
            if height != width:
                size = max(width, height)
                if size * size > MAX_PIXELS:
                    raise ImportError('square texture normalization exceeds pixel limit: ' + name)
                # Preserve normalized UVs when the fixed-square export slots resample a rectangular image.
                rows = []
                for y in range(height):
                    row = pixels[y * width * 4:(y + 1) * width * 4]
                    rows.append(b''.join(row[(x * width // size) * 4:(x * width // size + 1) * 4]
                                         for x in range(size)))
                pixels = b''.join(rows[y * height // size] for y in range(size))
                width = height = size
                self.warnings.add(f'{name}: rectangular texture resampled to a square with normalized UVs preserved')
            alpha_sum = sum(pixels[3::4])
            rgb = [sum(pixels[i] * pixels[i + 3 - c] for i in range(c, len(pixels), 4)) // max(1, alpha_sum) for c in range(3)]
            alpha = alpha_sum // (width * height)
            self.textures[name] = (encode_png(width, height, pixels), rgb, alpha)
        return name

    def remove_last_texture(self):
        name, _ = self.textures.popitem()
        self.face_texture_pixel_count -= self.face_texture_pixels.pop(name, 0)
        getattr(self, '_sign_checked_textures', {}).pop(name, None)

    def face_color_texture(self, texture, element, face, label):
        """Bake NeoForge's static vertex ARGB into the referenced texture."""
        if not any(key in owner for owner in (element, face) for key in ('neoforge_data', 'forge_data')):
            return texture
        if any('forge_data' in owner for owner in (element, face)):
            self.warnings.add(f'{label}: legacy forge_data color and lighting hints are not applied')
        if not any('neoforge_data' in owner for owner in (element, face)):
            return texture

        def metadata(value):
            if not isinstance(value, dict):
                raise ImportError('neoforge_data must be an object')
            color = value.get('color', -1)
            if isinstance(color, str):
                # ExtraFaceData uses Long.parseLong(hex,16), then the low32 bits.
                if not re.fullmatch(r'[+-]?[0-9a-fA-F]{1,16}', color):
                    raise ImportError('invalid NeoForge hexadecimal face color')
                color = int(color, 16)
                if not -(2**63) <= color < 2**63:
                    raise ImportError('NeoForge hexadecimal face color exceeds signed64 bits')
            elif type(color) not in (int, float) or not -(2**31) <= color < 2**32:
                raise ImportError('NeoForge face color must be a bounded 32-bit number or hexadecimal string')
            color = int(color)  # Codec.INT uses Number.intValue (truncate toward zero).
            lights = [value.get(key, 0) for key in ('block_light', 'sky_light')]
            if any(type(light) not in (int, float) or not -1 < light < 16 for light in lights):
                raise ImportError('NeoForge face light hints must truncate to integers 0..15')
            lights = [int(light) for light in lights]
            ao = value.get('ambient_occlusion', True)
            if type(ao) is not bool:
                raise ImportError('NeoForge ambient_occlusion must be boolean')
            return color & 0xffffffff, lights, ao

        # A face object replaces the entire element object, including defaults.
        inherited = metadata(element.get('neoforge_data', {}))
        color, lights, ao = metadata(face['neoforge_data']) if 'neoforge_data' in face else inherited
        if any(lights):
            self.warnings.add(f'{label}: NeoForge face lightmap hints are not exported as material emission')
        if not ao:
            self.warnings.add(f'{label}: NeoForge face ambient-occlusion setting is not reproduced')
        if color == 0xffffffff:
            return texture
        derived = 'mineways:face/tint_' + hashlib.sha256((texture + ':' + f'{color:08x}').encode()).hexdigest()
        if derived not in self.textures:
            width, height, pixels = decode_png(self.textures[texture][0])
            if self.face_texture_pixel_count + width * height > _obj_module.MAX_GENERATED_PIXELS:
                raise ImportError('face generated-texture pixel budget exceeded')
            rgba = ((color >> 16) & 255, (color >> 8) & 255, color & 255, (color >> 24) & 255)
            tinted = bytes(channel * rgba[index % 4] // 255 for index, channel in enumerate(pixels))
            alpha_sum = sum(tinted[3::4])
            rgb = [sum(tinted[i] * tinted[i + 3 - c] for i in range(c, len(tinted), 4)) // max(1, alpha_sum) for c in range(3)]
            self.textures[derived] = (encode_png(width, height, tinted), rgb, alpha_sum // (width * height))
            self.face_texture_pixels[derived] = width * height
            self.face_texture_pixel_count += width * height
        return derived

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

    def loader_texture(self, model, key):
        """Resolve a required custom-loader texture without inventing a missing asset."""
        texture = '#' + key
        seen = set()
        while isinstance(texture, str) and texture.startswith('#'):
            if texture in seen or len(seen) >= 64:
                raise ImportError('cyclic custom-loader texture references')
            seen.add(texture)
            if texture[1:] not in model['textures']:
                raise ImportError('missing custom-loader texture: ' + texture)
            texture = model['textures'][texture[1:]]
        return resource(texture)

    def custom_loader_adapter(self, name, model, selector):
        """Static asset-backed paths verified against the installed mod loader classes."""
        loader = model.get('loader')
        if loader == 'supplementaries:extra_rotation':
            elements = model.get('elements')
            if not isinstance(elements, list) or not elements or len(elements) > MAX_ELEMENTS:
                raise ImportError('extra-rotation model requires bounded elements')
            adjusted = []
            for element in elements:
                if not isinstance(element, dict):
                    raise ImportError('invalid extra-rotation element')
                element = dict(element)
                if 'extra_rotation' in element:
                    rotation = element.get('rotation')
                    if not isinstance(rotation, dict) or rotation.get('axis') not in ('x', 'y', 'z'):
                        raise ImportError('extra rotation requires an element rotation axis')
                    angle = number(rotation.get('angle', 0), -45, 45) - number(element.pop('extra_rotation'), -360, 360)
                    angle = number(angle, -45, 45)
                    if rotation.get('rescale', False) and abs(angle) not in (0, 22.5, 45):
                        raise ImportError('nonstandard extra rotation with rescale is unsupported')
                    # AwningModelLoader subtracts extra_rotation from the existing angle.
                    element['rotation'] = {**rotation, 'angle': angle}
                adjusted.append(element)
            return {**{key: value for key, value in model.items() if key not in ('loader', 'parent')},
                    'elements': adjusted}
        if loader not in ('refinedstorage:cable', 'bibliocraft:bookcase',
                          'bibliocraft:table', 'xycraft_core:connected_textures'):
            return None
        if any(key in model for key in ('transform', 'visibility')):
            raise ImportError('custom-loader root transforms and visibility require explicit replacement models')
        if loader == 'refinedstorage:cable':
            # CableUnbakedGeometry bakes this core. CableBakedModel adds extensions
            # only when its CABLE_CONNECTIONS ModelData supplies neighbor directions.
            color = model.get('color')
            if color not in ('white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime',
                             'pink', 'gray', 'light_gray', 'cyan', 'purple', 'blue',
                             'brown', 'green', 'red', 'black'):
                raise ImportError('invalid Refined Storage cable color')
            self.warnings.add(f'{name}: isolated cable core from supplied assets; neighbor connections and cable extensions require model data')
            return 'refinedstorage:block/cable/core/' + color
        if loader == 'bibliocraft:table':
            # TableModel.getQuads selects exactly this child from TableBlock.TYPE.
            kind = (selector or {}).get('type', 'none')
            if kind not in ('none', 'one', 'straight', 'curve', 'three', 'all'):
                raise ImportError('invalid Bibliocraft table type selector')
            child = model.get(kind)
            if not isinstance(child, dict):
                raise ImportError('missing Bibliocraft table child: ' + kind)
            return child
        if loader == 'bibliocraft:bookcase':
            # The loader removes its own marker and deserializes the remaining
            # ordinary BlockModel. Books are appended only by block-entity ModelData.
            self.warnings.add(f'{name}: supplied bookcase base only; inventory books and seasonal book variants require block-entity data')
            return {key: value for key, value in model.items() if key not in ('loader', 'parent')}
        # Xycraft builds four standard quadrants per enabled cube face. NONE
        # adjacency uses texture_single with unmodified UVs; merge the coplanar
        # quadrants and optional base layer into equivalent isolated cube faces.
        faces = model.get('faces', list(DIRECTIONS))
        if not isinstance(faces, list) or not faces or len(faces) > 6 or any(side not in DIRECTIONS for side in faces):
            raise ImportError('invalid Xycraft connected-texture face mask')
        connection = model.get('connection_handler', 'delegate')
        if not isinstance(connection, str) or (connection not in ('delegate', 'same_block') and not connection.startswith('#')):
            raise ImportError('invalid Xycraft connection handler')
        texture = self.loader_texture(model, 'texture_single')
        if 'base' in model['textures']:
            base = self.loader_texture(model, 'base')
            digest = hashlib.sha256((base + '\n' + texture).encode()).hexdigest()[:24]
            texture = self.overlay_texture('mineways:xycraft/isolated_' + digest, base, texture)
        self.warnings.add(f'{name}: Xycraft isolated texture; neighbor connections, dynamic tint and light emission are not evaluated')
        return {'elements': [{'from': [0, 0, 0], 'to': [16, 16, 16],
                              'faces': {side: {'texture': texture, 'uv': [0, 0, 16, 16],
                                               'cullface': side} for side in faces}}]}

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

    def compile_model(self, application, depth=0, selector=None, block_name=None):
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
        z = 0
        if 'ae2:z' in application:
            z = application['ae2:z']
            if type(z) is not int or z not in (0, 90, 180, 270):
                raise ImportError('invalid AE2 Z rotation')
            orientation = quarter_turn_orientation(x, y, z)
            equivalent = next(((a, b) for a in (0, 90, 180, 270) for b in (0, 90, 180, 270)
                               if quarter_turn_orientation(a, b) == orientation), None)
            if equivalent is not None:
                x, y = equivalent
                z = 0
        uvlock = application.get('uvlock', False)
        if not isinstance(uvlock, bool):
            raise ImportError('uvlock must be boolean')
        rotation = (x, y, int(uvlock), z) if z else (x, y, int(uvlock))
        model = self.model(application['model'])
        name = application['model']
        if _canvas_sign_module.matches(block_name, name, model):
            return _canvas_sign_module.compile_canvas_sign(self, name, model, rotation, selector, depth, ImportError, block_name, decode_png)
        if _twilight_sign_module.matches(block_name, model):
            return _twilight_sign_module.compile_sign(self, name, model, rotation, selector, depth, ImportError, block_name, decode_png, _canvas_sign_module)
        if _chest_module.is_twilight_chest(block_name, model):
            return _chest_module.compile_twilight_chest(self, name, model, rotation, selector, depth, ImportError, block_name, decode_png)
        if _machine_module.is_empty_fluid(block_name, name, model):
            self.warnings.add(block_name + ': verified empty industrial fluid placeholder; schematic identity is preserved without geometry')
            return {'rotation': rotation, 'elements': [], 'invisible': True}
        if name == 'ae2:block/matrix_frame' and model == {'textures': {}}:
            return {'rotation': rotation, 'elements': [], 'invisible': True}
        model = self.builtin_adapter(name, model)
        if depth and any(flag not in (selector or {}) for flag in PALETTE_FLAGS.get(model.get('loader'), ())):
            raise ImportError('nested dynamic model requires explicit palette flags')
        if model.get('loader') == _mekanism_module.DATA_LOADER:
            return _mekanism_module.compile_data_based(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _conveyor_module.LOADER:
            return _conveyor_module.compile_conveyor(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _faucet_module.LOADER:
            return _faucet_module.compile_faucet(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _storage_module.PORTABLE_LOADER:
            return _storage_module.compile_portable_grid(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _chest_module.LOADER:
            return _chest_module.compile_chest(self, name, model, rotation, selector, depth, ImportError, block_name, decode_png)
        if model.get('loader') == _xycraft_pipe_module.LOADER:
            return _xycraft_pipe_module.compile_pipe(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _shulker_module.LOADER:
            return _shulker_module.compile_shulker(self, name, model, rotation, selector, depth, ImportError, block_name, decode_png)
        if model.get('loader') == _pressure_tube_module.LOADER:
            return _pressure_tube_module.compile_pressure_tube(self, name, model, rotation, selector, depth, ImportError, block_name, quarter_turn_orientation)
        if model.get('loader') == _storage_module.BARREL_LOADER:
            return _storage_module.compile_barrel(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _backpack_module.LOADER:
            return _backpack_module.compile_backpack(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') in _twilight_module.LOADERS:
            return _twilight_module.compile_twilight(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') in _power_module.LOADERS:
            return _power_module.compile_power_model(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _custom_defaults.FRAMED_LOADER:
            return _custom_defaults.compile_framed(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _custom_defaults.MICROCHIP_LOADER:
            return _custom_defaults.compile_microchip(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _lantern_module.LOADER:
            return _lantern_module.compile_wall_lantern(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _ie_sides_module.LOADER:
            return _ie_sides_module.compile_conf_sides(self, name, model, rotation, selector, depth, ImportError, block_name)
        if _wire_module.is_empty_rack_model(name, model, selector, block_name):
            return {'rotation': rotation, 'elements': [], 'invisible': True}
        if model.get('loader') in _wire_module.LOADERS:
            return _wire_module.compile_wire_model(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _framework_module.LOADER:
            model = _framework_module.adapt_open_model(model, ImportError, MAX_ELEMENTS)
        if model.get('loader') == _enderio_module.IO_OVERLAY_LOADER:
            return _enderio_module.compile_io_overlay(self, name, rotation)
        if model.get('loader') == _material_module.DOMUM_LOADER:
            return _material_module.compile_domum(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _storage_module.SIMPLE_MATERIAL_LOADER:
            return _storage_module.compile_simple_material(self, name, model, rotation, selector, depth, ImportError, block_name)
        if model.get('loader') == _storage_module.LIMITED_BARREL_LOADER:
            return _storage_module.compile_limited_barrel(self, name, model, rotation, selector, depth, ImportError, block_name)
        if (name == 'immersiveengineering:block/ie_empty' or model.get('parent') == 'immersiveengineering:block/ie_empty') and model.get('elements') == [] and 'loader' not in model:
            fake_light = block_name == 'immersiveengineering:fake_light' and name == 'immersiveengineering:block/ie_empty'
            turret_top = (block_name in ('immersiveengineering:turret_chem', 'immersiveengineering:turret_gun')
                          and (selector or {}).get('multiblockslave') == 'true'
                          and name == block_name.replace(':', ':block/') + '_top')
            if not fake_light and not turret_top:
                raise ImportError('IE block-entity renderer has no supported static geometry: ' + str(name))
            if turret_top:
                self.warnings.add(str(name) + ': verified empty turret upper cell; the base callback reports omitted block-entity gun geometry')
            return {'rotation': rotation, 'elements': [], 'invisible': True}
        if model.get('loader') == 'immersiveengineering:basic_split':
            return _split_module.compile_split(self, name, model, rotation, selector, depth, ImportError)
        if model.get('loader') in ('neoforge:obj', 'immersiveengineering:ie_obj', _mekanism_module.LOADER):
            obj_api = SimpleNamespace(ImportError=ImportError, resource=resource, decode_png=decode_png,
                                      encode_png=encode_png, MAX_COMPILED_ELEMENTS=MAX_COMPILED_ELEMENTS,
                                      number=number, expanded_uv=expanded_uv, REPEATED_UV_WARNING=REPEATED_UV_WARNING)
            if model['loader'] == 'immersiveengineering:ie_obj':
                return _ie_module.compile_ie_obj(self, model, rotation, selector, obj_api, _obj_module)
            if model['loader'] == _mekanism_module.LOADER:
                return _mekanism_module.compile_transmitter(self, model, rotation, obj_api, _obj_module)
            return _obj_module.compile_obj(self, model, rotation, obj_api)
        if model.get('loader') == 'fusion:model':
            label = name if isinstance(name, str) else '<inline>'
            self.warnings.add(f'{label}: connected-texture neighbors are not evaluated')
        if model.get('loader') in _machine_module.LOADERS:
            return _machine_module.compile_machine(self, name, model, rotation, selector, depth, ImportError, block_name)
        adapted = self.custom_loader_adapter(name, model, selector)
        if adapted is not None:
            return self.compile_model({'model': adapted, 'x': x, 'y': y, 'uvlock': uvlock, 'ae2:z': z}, depth + 1, selector, block_name)
        if model.get('loader') == 'lootr:brushable':
            stage = (selector or {}).get('dusted')
            if stage not in ('0', '1', '2', '3'):
                raise ImportError('Lootr brushable model requires an explicit dusted=0..3 selector')
            child = model.get('stage_' + stage)
            if not isinstance(child, dict):
                raise ImportError('missing Lootr brushable stage model: ' + stage)
            self.warnings.add(f'{application["model"]}: Lootr uses dusted stage without player-opened appearance; player data is not in the block palette')
            return self.compile_model({'model': child, 'x': x, 'y': y, 'uvlock': uvlock, 'ae2:z': z}, depth + 1, selector, block_name)
        if model.get('loader') in ('neoforge:composite', 'forge:composite'):
            transform = _obj_module.model_transform(model.get('transform', {}), ImportError)
            visibility = model.get('visibility', {})
            if not isinstance(visibility, dict) or len(visibility) > 4096 or any(type(v) is not bool for v in visibility.values()):
                raise ImportError('invalid composite visibility')
            children = model.get('children')
            if not isinstance(children, dict) or not children or len(children) > 256:
                raise ImportError('invalid or excessive composite children')
            if not all(isinstance(child, dict) for child in children.values()):
                raise ImportError('composite child must be an inline model object')
            order = model.get('item_render_order', list(children))
            if (not isinstance(order, list) or len(order) > 256
                    or any(not isinstance(key, str) or key not in children for key in order)):
                raise ImportError('composite item_render_order requires bounded child names')
            elements, polygons = [], []
            all_invisible = True
            requires_visible_companion = False
            fixed_rotation = False
            for child_name, child in children.items():
                if not visibility.get(child_name, True):
                    continue
                compiled = self.compile_model({'model': child}, depth + 1, selector, block_name)
                if 'split_models' in compiled or 'split_policy' in compiled:
                    raise ImportError('nested split model geometry requires an explicit replacement model')
                child_rotation = compiled.get('rotation', (0, 0, 0))
                if child_rotation[0] or child_rotation[1] or (len(child_rotation) > 3 and child_rotation[3]):
                    raise ImportError('composite child rotation requires an explicit replacement model')
                fixed_rotation = fixed_rotation or compiled.get('fixed_rotation', False)
                if fixed_rotation and (rotation[0] or rotation[1] or (len(rotation) > 3 and rotation[3]) or not transform.is_identity):
                    raise ImportError('rotated composite contains fixed-rotation geometry')
                all_invisible = all_invisible and compiled.get('invisible', False)
                requires_visible_companion = requires_visible_companion or compiled.get('requires_visible_companion', False)
                elements.extend(compiled['elements'])
                polygons.extend(compiled.get('polygons', []))
            if not transform.is_identity:
                if elements:
                    raise ImportError('transformed composites with standard box elements are unsupported')
                polygons = [{'texture': p['texture'], 'vertices': [(*transform(v[:3]), *v[3:]) for v in p['vertices']]} for p in polygons]
            if len(polygons) > _obj_module.MAX_POLYGONS:
                raise ImportError('composite polygon count exceeds limit')
            if len(elements) > MAX_ELEMENTS:
                raise ImportError('composite element count exceeds limit')
            result = {'rotation': rotation, 'elements': elements, 'polygons': polygons, 'invisible': all_invisible}
            if fixed_rotation:
                result['fixed_rotation'] = True
            if all_invisible and requires_visible_companion:
                result['requires_visible_companion'] = True
            return result
        elements = model.get('elements')
        if not isinstance(elements, list) or not elements or len(elements) > MAX_ELEMENTS:
            raise ImportError('model needs 1..2048 standard elements: ' + str(application['model']))
        result = {'rotation': rotation, 'elements': []}
        for element in elements:
            if not isinstance(element, dict):
                raise ImportError('invalid model element')
            start = vector(element.get('from'), 3, MIN_MODEL_COORDINATE, MAX_MODEL_COORDINATE)
            end = vector(element.get('to'), 3, MIN_MODEL_COORDINATE, MAX_MODEL_COORDINATE)
            # Preserve signed extents: the original face corner order and UV pairing
            # determine the winding of inside-out and thin inverted elements.
            rotation = element.get('rotation', {})
            if not isinstance(rotation, dict):
                raise ImportError('invalid element rotation')
            axis = rotation.get('axis', 'none')
            angle = number(rotation.get('angle', 0), -45, 45)
            if axis not in ('none', 'x', 'y', 'z') or (axis == 'none' and angle != 0) or any(key in rotation for key in ('x', 'y', 'z')):
                raise ImportError('unsupported element rotation')
            origin = vector(rotation.get('origin', [8, 8, 8]), 3, MIN_MODEL_COORDINATE, MAX_MODEL_COORDINATE)
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
                normal_axis = {'down': 1, 'up': 1, 'north': 2, 'south': 2, 'west': 0, 'east': 0}[side]
                # Match saveModGeometry: these faces have no geometric area even
                # after element rotations. Their textures and UVs are never used.
                if any(start[i] == end[i] for i in range(3) if i != normal_axis):
                    continue
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
                label = '<inline model>' if isinstance(application['model'], dict) else application['model']
                texture = self.face_color_texture(texture, element, face, label)
                uv = vector(face.get('uv', default_uv(side, start, end)), 4, MIN_MODEL_UV, MAX_MODEL_UV)
                if expanded_uv(((uv[0], uv[1]), (uv[2], uv[3]))):
                    label = '<inline model>' if isinstance(application['model'], dict) else application['model']
                    self.warnings.add(f'{label}: {REPEATED_UV_WARNING}')
                turn = face.get('rotation', 0)
                if type(turn) is not int or turn not in (0, 90, 180, 270):
                    raise ImportError('invalid face rotation')
                cull = face.get('cullface', '-')
                if not isinstance(cull, str) or len(cull) > 1023:
                    raise ImportError('invalid cullface')
                if cull not in (*DIRECTIONS, '-'):
                    # Minecraft Direction.byName returns null for unknown names.
                    # Preserve that face without applying neighbor culling.
                    self.warnings.add(f'{label}: unknown cullface {cull!r} disables neighbor culling as in Minecraft')
                    cull = '-'
                if face.get('tintindex', -1) != -1:
                    self.warnings.add(f'{application["model"]}: biome/custom tint is not applied')
                compiled['faces'].append((side, *uv, turn, texture, cull))
            if compiled['faces']:
                self.element_count += 1
                if self.element_count > MAX_COMPILED_ELEMENTS:
                    raise ImportError('compiled element budget exceeded')
                result['elements'].append(compiled)
        if not result['elements']:
            raise ImportError('model has no nonzero-area faces: ' + str(application['model']))
        return result

    def block(self, name, definition):
        # Expanders compare selector strings. Canonicalize supplied keys first,
        # so an explicit unsorted selector cannot evade their overlap checks.
        if 'variants' in definition:
            variants = definition['variants']
            if not isinstance(variants, dict) or not variants or len(variants) > MAX_STATES:
                raise ImportError('invalid or excessive block variants')
            normalized = {}
            for selector, application in variants.items():
                if not isinstance(selector, str):
                    raise ImportError('invalid state selector')
                values = properties(selector)
                key = ','.join(f'{key}={values[key]}' for key in sorted(values))
                if key in normalized:
                    # Python equality conflates bool/int/float JSON values.
                    if json.dumps(normalized[key], sort_keys=True, separators=(',', ':')) != json.dumps(application, sort_keys=True, separators=(',', ':')):
                        raise ImportError('conflicting canonical state selector: ' + key)
                else:
                    normalized[key] = application
            definition = {**definition, 'variants': normalized}
        definition = _twilight_sign_module.expand_sign(self, name, definition, ImportError, _canvas_sign_module)
        definition = _canvas_sign_module.expand_canvas_sign(self, name, definition, ImportError)
        definition = _conveyor_module.expand_conveyor(self, name, definition, ImportError)
        definition = _chest_module.expand_chest(self, name, definition, ImportError)
        definition = _storage_module.expand_portable_grid(self, name, definition, ImportError)
        definition = _shulker_module.expand_shulker(self, name, definition, ImportError)
        for loader, flags in PALETTE_FLAGS.items():
            definition = _storage_module.expand_loader_flags(self, name, definition, loader, flags, ImportError)
        definition = _storage_module.expand_block(self, name, definition, ImportError)
        definition = _ie_module.expand_block(self, name, definition, ImportError)
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
                        elif type(value) is int:
                            # Minecraft Selector.Deserializer uses getAsString.
                            # Keep numeric property identity; do not round floats
                            # or treat booleans as the integers 0 and 1.
                            try:
                                value = str(value)
                            except ValueError as error:
                                raise ImportError('invalid multipart property number') from error
                            condition[key] = value
                        if not isinstance(key, str) or not PROPERTY.fullmatch(key) or not isinstance(value, str):
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
                models = [self.compile_model(value, selector=selector, block_name=name) for value in applications]
                models = [part for model in models for part in model.get('split_models', [model])]
                if len(models) > 256:
                    raise ImportError('state has more than 256 compiled model parts')
                split_policies = {tuple(model['split_policy']) for model in models if 'split_policy' in model}
                if split_policies and (len(split_policies) != 1 or any('split_policy' not in model for model in models)):
                    raise ImportError('state combines incompatible IE split providers')
                if models and all(model.get('invisible') for model in models):
                    if any(model.get('requires_visible_companion') for model in models):
                        raise ImportError('model requires live renderer data without a visible supplied fallback')
                    output.append((canonical(name, selector), [0, 0, 0], 0, []))
                    continue
                faces = [face for model in models for element in model['elements'] for face in element['faces']]
                polygons = [face for model in models for face in model.get('polygons', [])]
                if not faces and not polygons:
                    raise ImportError('state has no standard model faces')
                texture = next((face[-2] for face in faces if face[0] == 'up'), faces[0][-2] if faces else polygons[0]['texture'])
                _, color, alpha = self.textures[texture]
                output.append((canonical(name, selector), color, alpha, models))
            except (ImportError, RecursionError) as error:
                self.element_count = before_count
                while len(self.textures) > before_textures:
                    self.remove_last_texture()
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


def missing_resource_diagnostics(report, assets):
    # Namespace presence describes supplied assets, not installed mods or registered blocks.
    namespaces = {path.split('/')[1] for path in assets.files if path.startswith('assets/')}
    counts = {}
    for field in ('unsupported', 'unsupported_states'):
        totals = {'namespace_unindexed': 0, 'namespace_indexed': 0}
        for row in report[field]:
            match = re.fullmatch(r'missing (texture|model): ([a-z0-9_.-]+:[a-z0-9_./-]+)', row['reason'])
            if not match:
                continue
            kind, name = match.groups()
            present = name.split(':', 1)[0] in namespaces
            row['missing_resource'] = {'kind': kind, 'name': name, 'indexed_namespace_present': present}
            totals['namespace_indexed' if present else 'namespace_unindexed'] += 1
        counts[field] = totals
    report['missing_resources'] = counts


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
    polygon_count = 0
    has_z_rotation = False
    has_expanded_uv = False
    has_split_geometry = False
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
                        compiler.remove_last_texture()
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
                    if models and 'split_policy' in models[0]:
                        has_split_geometry = True
                        state_stream.write('splitpolicy\t' + '\t'.join(map(str, models[0]['split_policy'])) + '\n')
                    for model in models:
                        # An explicitly invisible multipart child contributes no geometry.
                        # All-invisible states were normalized to [] by Compiler.block.
                        if model.get('invisible'):
                            continue
                        if 'part' in model and not model['elements'] and not model.get('polygons'):
                            state_stream.write('emptypart\t' + '\t'.join(map(str, model['part'])) + '\n')
                            continue
                        has_z_rotation = has_z_rotation or len(model['rotation']) == 4
                        signature = hashlib.sha256(json.dumps((model['elements'], model.get('polygons', [])), sort_keys=True, separators=(',', ':')).encode()).digest()
                        index = geometry_ids.get(signature)
                        if index is None:
                            index = len(geometry_ids)
                            element_count += len(model['elements'])
                            if index >= MAX_GEOMETRIES or element_count > MAX_COMPILED_ELEMENTS:
                                raise ImportError('compiled shared geometry exceeds runtime limit')
                            polygon_count += len(model.get('polygons', []))
                            if polygon_count > 1000000:
                                raise ImportError('compiled polygon catalog exceeds runtime limit')
                            geometry_ids[signature] = index
                            geometry_stream.write(f'geometry\t{index}\n')
                            for element in model['elements']:
                                geometry_stream.write('element\t' + '\t'.join(map(str, element['coordinates'])) + '\n')
                                for face in element['faces']:
                                    has_expanded_uv = has_expanded_uv or any(v < 0 or v > 16 for v in face[1:5])
                                    texture = face[-2]
                                    if texture not in texture_ids:
                                        if len(texture_ids) >= MAX_TEXTURES:
                                            raise ImportError('compiled texture catalog exceeds runtime limit')
                                        texture_ids[texture] = len(texture_ids)
                                    fields = (*face[:-2], texture_ids[texture], face[-1])
                                    geometry_stream.write('face\t' + '\t'.join(map(str, fields)) + '\n')
                            for polygon in model.get('polygons', []):
                                has_expanded_uv = has_expanded_uv or any(v < 0 or v > 16 for vertex in polygon['vertices'] for v in vertex[3:])
                                texture = polygon['texture']
                                if texture not in texture_ids:
                                    if len(texture_ids) >= MAX_TEXTURES:
                                        raise ImportError('compiled texture catalog exceeds runtime limit')
                                    texture_ids[texture] = len(texture_ids)
                                fields = (texture_ids[texture], len(polygon['vertices']),
                                          *(v for vertex in polygon['vertices'] for v in vertex))
                                geometry_stream.write('polygon\t' + '\t'.join(map(str, fields)) + '\n')
                            geometry_stream.write('endgeometry\n')
                        state_stream.write('usemodel\t' + '\t'.join(map(str, (index, *model['rotation']))) + '\n')
                        if 'part' in model:
                            state_stream.write('part\t' + '\t'.join(map(str, model['part'])) + '\n')
                    state_stream.write('endstate\n')
                if geometry_stream.tell() + state_stream.tell() > MAX_MANIFEST:
                    raise ImportError('compiled manifest exceeds runtime size limit')
        if not block_count and not dimensions:
            if not rejected and not unsupported_dimensions:
                raise NoModDefinitions('no supported mod blocks or dimensions found')
            raise ImportError('no supported mod blocks or dimensions found' + (': ' + rejected[0]['reason'] if rejected else ''))
        version = 6 if has_split_geometry else 5 if has_expanded_uv else 4 if has_z_rotation else 3 if polygon_count else 2
        report = {'format': version, 'polygons': polygon_count, 'blocks': block_count, 'selectors': selector_count,
                  'textures': len(texture_ids), 'geometries': len(geometry_ids), 'elements': element_count,
                  'dimensions': len(dimensions), 'unsupported_dimensions': unsupported_dimensions,
                  'unsupported': rejected, 'unsupported_states': compiler.unsupported_states,
                  'warnings': sorted(compiler.warnings)}
        missing_resource_diagnostics(report, assets)
        with (temporary / 'manifest.mwpack').open('w', encoding='utf-8', newline='\n') as manifest:
            manifest.write(f'MINEWAYS_MOD_PACK\t{version}\n')
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
