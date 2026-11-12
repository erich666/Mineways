#!/usr/bin/env python3
"""Compile Minecraft asset folders/JARs/ZIPs to a data-only Mineways mod pack."""

import argparse
import binascii
import itertools
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
MAX_FILES = 100000
MAX_STATES = 4096
MAX_ELEMENTS = 2048
MAX_PIXELS = 4 * 1024 * 1024
MAX_COMPILED_ELEMENTS = 65536
# Match vacant cells in Win/tiles.h; the importer test checks this release budget.
MAX_TEXTURES = 915
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
    def __init__(self, sources):
        self.files = {}
        total = 0
        count = 0
        for source in map(Path, sources):
            if source.is_symlink():
                raise ImportError('source must not be a symbolic link: ' + str(source))
            if source.is_dir():
                root = source.parent if source.name == 'assets' else source
                for directory, dirs, files in os.walk(root, followlinks=False):
                    for name in dirs + files:
                        if (Path(directory) / name).is_symlink():
                            raise ImportError('symbolic links are not accepted: ' + str(Path(directory) / name))
                    for name in sorted(files):
                        path = Path(directory) / name
                        relative = safe_path(path.relative_to(root).as_posix())
                        count += 1
                        if count > MAX_FILES:
                            raise ImportError('too many source files')
                        if not self.wanted(relative):
                            continue
                        size = path.stat().st_size
                        total += size
                        if size > MAX_FILE or total > MAX_TOTAL:
                            raise ImportError('source asset size limit exceeded')
                        self.files[relative] = path.read_bytes()
            elif source.is_file():
                with zipfile.ZipFile(source) as archive:
                    seen = set()
                    for entry in archive.infolist():
                        count += 1
                        if count > MAX_FILES:
                            raise ImportError('too many archive entries')
                        # ZipInfo normalizes separators on Windows and truncates NULs.
                        # Validate the original archive spelling before using its normalized name.
                        name = safe_path(entry.orig_filename)
                        if name in seen:
                            raise ImportError('duplicate archive entry: ' + name)
                        seen.add(name)
                        if stat.S_ISLNK(entry.external_attr >> 16):
                            raise ImportError('archive symbolic links are not accepted')
                        if not self.wanted(name) or entry.is_dir():
                            continue
                        total += entry.file_size
                        if entry.file_size > MAX_FILE or total > MAX_TOTAL:
                            raise ImportError('archive asset size limit exceeded')
                        if entry.file_size > max(1024 * 1024, entry.compress_size * 1000):
                            raise ImportError('archive compression ratio limit exceeded')
                        with archive.open(entry) as stream:
                            data = stream.read(MAX_FILE + 1)
                        if len(data) > MAX_FILE:
                            raise ImportError('archive asset size limit exceeded')
                        self.files[name] = data
            else:
                raise ImportError('source does not exist: ' + str(source))
        if not any('/blockstates/' in key or '/dimension/' in key for key in self.files):
            raise ImportError('no blockstate assets or dimension definitions found')

    @staticmethod
    def wanted(name):
        return ((name.startswith('assets/') and name.endswith(('.json', '.png')))
                or (name.startswith('data/') and name.endswith('.json') and ('/dimension/' in name or '/dimension_type/' in name)))

    def json(self, path):
        if path not in self.files:
            raise ImportError('missing asset: ' + path)
        try:
            value = json.loads(self.files[path], parse_constant=lambda s: (_ for _ in ()).throw(ImportError('nonfinite JSON number')))
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


def decode_png(data):
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
            if depth != 8 or not channels or comp or filt or interlace:
                raise ImportError('PNG requires non-interlaced 8-bit pixels')
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
    if not ended or offset != len(data):
        raise ImportError('missing PNG end or trailing data')
    expected = height * (1 + width * channels)
    decoder = zlib.decompressobj()
    try:
        raw = decoder.decompress(compressed, expected + 1)
    except zlib.error as error:
        raise ImportError('invalid PNG compression') from error
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ImportError('PNG decompressed size mismatch')
    stride, output, previous = width * channels, bytearray(), bytearray(width * channels)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ImportError('invalid PNG row filter')
        for i in range(stride):
            a = row[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
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
        for i in range(0, stride, channels):
            px = row[i:i + channels]
            if color == 6:
                output.extend(px)
            elif color == 2:
                alpha = 0 if len(transparency) == 6 and tuple(px) == struct.unpack('>3H', transparency) else 255
                output.extend((*px, alpha))
            elif color == 0:
                alpha = 0 if len(transparency) == 2 and px[0] == struct.unpack('>H', transparency)[0] else 255
                output.extend((px[0], px[0], px[0], alpha))
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


class Compiler:
    def __init__(self, assets):
        self.assets = assets
        self.models = {}
        self.textures = {}
        self.warnings = set()
        self.element_count = 0

    def model(self, name, chain=()):
        name = resource(name)
        if name in chain or len(chain) >= 64:
            raise ImportError('cyclic or excessive model parents: ' + name)
        if name in self.models:
            return self.models[name]
        path = asset_path(name, 'models')
        own = self.assets.json(path) if path in self.assets.files else cube_template(name)
        if own is None:
            raise ImportError('missing model: ' + name)
        if 'loader' in own:
            raise ImportError('custom model loader: ' + str(own['loader']))
        parent = self.model(own['parent'], chain + (name,)) if 'parent' in own else {}
        result = {**parent, **own}
        if not isinstance(own.get('textures', {}), dict):
            raise ImportError('model textures must be an object: ' + name)
        result['textures'] = {**parent.get('textures', {}), **own.get('textures', {})}
        self.models[name] = result
        return result

    def texture(self, name):
        name = resource(name)
        if name not in self.textures:
            path = asset_path(name, 'textures', '.png')
            if path not in self.assets.files:
                raise ImportError('missing texture: ' + name)
            width, height, pixels = decode_png(self.assets.files[path])
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

    def compile_model(self, application):
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
        uvlock = application.get('uvlock', False)
        if not isinstance(uvlock, bool):
            raise ImportError('uvlock must be boolean')
        model = self.model(application['model'])
        elements = model.get('elements')
        if not isinstance(elements, list) or not elements or len(elements) > MAX_ELEMENTS:
            raise ImportError('model needs 1..2048 standard elements: ' + application['model'])
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
                    texture = model['textures'].get(texture[1:], '')
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
        for selector, applications in selectors:
            if len(applications) > 256:
                raise ImportError('state has more than 256 model applications')
            models = [self.compile_model(value) for value in applications]
            faces = [face for model in models for element in model['elements'] for face in element['faces']]
            face = next((face for face in faces if face[0] == 'up'), faces[0])
            _, color, alpha = self.textures[face[-2]]
            output.append((canonical(name, selector), color, alpha, models))
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
    states, rejected = [], []
    block_count = 0
    for path in sorted(assets.files):
        match = re.fullmatch(r'assets/([a-z0-9_.-]+)/blockstates/(.+)\.json', path)
        if not match or (namespaces and match[1] not in namespaces) or (not namespaces and match[1] == 'minecraft'):
            continue
        name = resource(match[1] + ':' + match[2])
        previous_elements = compiler.element_count
        previous_textures = set(compiler.textures)
        previous_warnings = compiler.warnings.copy()
        try:
            block = compiler.block(name, assets.json(path))
            if len(states) + len(block) > 65536 or block_count >= 3522:
                raise ImportError('compiled block or selector count exceeds runtime limit')
            states.extend(block)
            block_count += 1
        except (ImportError, RecursionError) as error:
            compiler.element_count = previous_elements
            for texture in set(compiler.textures) - previous_textures:
                del compiler.textures[texture]
            compiler.warnings = previous_warnings
            rejected.append({'block': name, 'reason': str(error)})
    if not states and not dimensions:
        raise ImportError('no supported mod blocks or dimensions found' + (': ' + rejected[0]['reason'] if rejected else ''))
    used = sorted({face[-2] for _, _, _, models in states for model in models for element in model['elements'] for face in element['faces']})
    if len(used) > MAX_TEXTURES:
        raise ImportError(f'pack needs {len(used)} textures, but this Mineways release has {MAX_TEXTURES} vacant atlas cells; select fewer mods or namespaces')
    texture_ids = {name: index for index, name in enumerate(used)}
    lines = ['MINEWAYS_MOD_PACK\t1']
    for dimension in dimensions:
        lines.append('dimension\t' + '\t'.join(map(str, dimension)))
    for name, index in texture_ids.items():
        lines.append(f'texture\t{index}\t{name}\ttextures/{index:04}.png')
    for selector, color, alpha, models in states:
        lines.append(f'state\t{selector}\t' + ''.join(f'{c:02x}' for c in color) + f'\t{alpha}')
        for model in models:
            lines.append('model\t' + '\t'.join(map(str, model['rotation'])))
            for element in model['elements']:
                lines.append('element\t' + '\t'.join(map(str, element['coordinates'])))
                for face in element['faces']:
                    fields = (*face[:-2], texture_ids[face[-2]], face[-1])
                    lines.append('face\t' + '\t'.join(map(str, fields)))
        lines.append('endstate')
    report = {'format': 1, 'blocks': len({state.split('[')[0] for state, *_ in states}),
              'selectors': len(states), 'textures': len(used), 'dimensions': len(dimensions),
              'unsupported_dimensions': unsupported_dimensions,
              'unsupported': rejected, 'warnings': sorted(compiler.warnings)}
    temporary = Path(tempfile.mkdtemp(prefix='.mineways-pack-', dir=destination.parent))
    try:
        (temporary / 'textures').mkdir()
        for name, index in texture_ids.items():
            (temporary / 'textures' / f'{index:04}.png').write_bytes(compiler.textures[name][0])
        (temporary / 'manifest.mwpack').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        (temporary / 'report.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        temporary.rename(destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='+', help='asset directories or JAR/ZIP archives, later sources override earlier sources')
    parser.add_argument('-o', '--output', required=True, help='new output directory (existing output is never overwritten)')
    parser.add_argument('--namespace', action='append', help='only import this namespace (repeatable, default: all non-minecraft namespaces)')
    args = parser.parse_args()
    try:
        report = compile_pack(args.sources, args.output, args.namespace)
    except (ImportError, OSError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, f'Import failed: {error}\n')
    print(f'Created {args.output}: {report["blocks"]} blocks, {report["selectors"]} selectors, {report["textures"]} textures, {report["dimensions"]} dimensions')
    print(f'{len(report["unsupported"])} unsupported blocks; {len(report["unsupported_dimensions"])} incomplete dimensions; {len(report["warnings"])} warnings. Read report.json before use.')


if __name__ == '__main__':
    main()
