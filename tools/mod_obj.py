"""Bounded static NeoForge OBJ geometry. No mod classes or external paths execute."""
import hashlib
import math
from pathlib import PurePosixPath

MAX_VERTICES = 131072
MAX_POLYGONS = 16384
MAX_LINES = 500000
MAX_LIBRARY_BYTES = 16 * 1024 * 1024
MAX_GENERATED_PIXELS = 64 * 1024 * 1024


def compile_obj(compiler, model, rotation, api, *, ie=None, visible_groups=None):
    error = api.ImportError
    if visible_groups is not None and (ie is not None or not isinstance(visible_groups, (set, frozenset))
            or len(visible_groups) > 4096 or any(not isinstance(name, str) or len(name) > 1023 for name in visible_groups)):
        raise error('invalid trusted OBJ visibility groups')
    visibility = model.get('visibility', {})
    if not isinstance(visibility, dict) or len(visibility) > 4096 or any(not isinstance(k, str) or type(v) is not bool for k, v in visibility.items()):
        raise error('invalid OBJ component visibility')
    if 'mtl_override' in model and (not isinstance(model['mtl_override'], str) or not model['mtl_override']):
        raise error('OBJ material override must be a nonempty resource string')
    coordinate_limit = getattr(api, 'source_coordinate_limit', 256)
    if type(coordinate_limit) is not int or not 256 <= coordinate_limit <= 4096:
        raise error('invalid trusted OBJ source coordinate limit')
    transform = model_transform(model.get('transform', {}), error, coordinate_limit)
    for key in ('flip_v', 'automatic_culling', 'shade_quads', 'emissive_ambient'):
        if key in model and type(model[key]) is not bool:
            raise error('OBJ option must be boolean: ' + key)
    name = api.resource(model.get('model'))
    namespace, relative = name.split(':')
    if not relative.startswith('models/'):
        raise error('OBJ reference must be inside models/')

    def resource_path(identifier):
        ns, path = api.resource(identifier).split(':')
        if not path.startswith('models/'):
            raise error('OBJ/MTL reference must be inside models/')
        return 'assets/' + ns + '/' + path

    library_cache = {}
    library_bytes = 0
    library_materials = 0
    source_lines = None

    def records(identifier, material_library=False):
        path = resource_path(identifier)
        if path not in compiler.assets.files:
            raise error('missing OBJ/MTL asset: ' + identifier)
        try:
            nonlocal source_lines
            data = compiler.assets.read(path) if material_library or source_lines is None else None
            if material_library:
                nonlocal library_bytes
                library_bytes += len(data)
                if library_bytes > MAX_LIBRARY_BYTES:
                    raise error('OBJ material-library byte budget exceeded')
            lines = data.decode('utf-8-sig').splitlines() if data is not None else source_lines
            if not material_library:
                source_lines = lines
        except UnicodeDecodeError as exc:
            raise error('OBJ/MTL must be UTF-8') from exc
        if len(lines) > MAX_LINES:
            raise error('OBJ/MTL line limit exceeded')
        for line in lines:
            if len(line) > 16384:
                raise error('OBJ/MTL line exceeds limit')
            text = line.strip()
            if text and not text.startswith('#'):
                # '#' starts texture-variable names in MTL, so only whitespace-separated comments are removed.
                yield text.split()

    def numbers(items, low, high):
        try:
            values = tuple(float(v) for v in items)
        except ValueError as exc:
            raise error('invalid OBJ/MTL number') from exc
        if any(not math.isfinite(v) or v < low or v > high for v in values):
            raise error('OBJ/MTL number outside supported bounds')
        return values

    def library(reference):
        if ':' not in reference:
            reference = namespace + ':' + str(PurePosixPath(relative).parent / reference)
        reference = api.resource(reference)
        if reference in library_cache:
            return library_cache[reference]
        if len(library_cache) >= 64:
            raise error('OBJ material-library count limit exceeded')
        # resource() rejects absolute paths, '..', Windows paths and URL schemes.
        materials = {}
        current = None
        for fields in records(reference, True):
            kind, args = fields[0], fields[1:]
            if kind == 'newmtl':
                nonlocal library_materials
                library_materials += 1
                if not args or library_materials > 4096:
                    raise error('invalid/excessive OBJ materials')
                label = ' '.join(args)
                if label in materials:
                    raise error('duplicate OBJ material')
                current = {'color': (1., 1., 1., 1.), 'map': None, 'ambient': (0., 0., 0.)}
                materials[label] = current
            elif ie is not None and kind != 'map_Kd':
                # IE model-splitter retains map_Kd only, unlike NeoForge OBJ.
                continue
            elif kind in ('Kd', 'Ka', 'map_Kd', 'forge_TintIndex', 'neoforge_TintIndex'):
                if current is None:
                    raise error('OBJ material property before newmtl')
                if kind in ('Kd', 'Ka'):
                    if len(args) not in (3, 4):
                        raise error('invalid OBJ material color')
                    color = numbers(args, 0, 1)
                    current['color' if kind == 'Kd' else 'ambient'] = color + ((1.,) if len(color) == 3 and kind == 'Kd' else ())
                elif kind == 'map_Kd':
                    if len(args) != 1:
                        raise error('OBJ texture map options are unsupported')
                    current['map'] = args[0]
                elif args != ['-1']:
                    compiler.warnings.add(name + ': OBJ biome/custom tint is not applied')
            # NeoForge does not use Ns/Ks/d/Tr for baked quad color or opacity.
        library_cache[reference] = materials
        return materials

    materials = library(model['mtl_override']) if model.get('mtl_override') else {}
    positions, texcoords, normals = [], [], []
    polygons, material = [], None
    resolved = {}
    uses_normals = False
    hidden_faces = 0
    group, part, object_above_group = '', None, False
    flip = model.get('flip_v', False)

    def texture(label, group):
        key = (label, ie.color(group, label)) if ie is not None else label
        if key in resolved:
            return resolved[key]
        if label not in materials:
            raise error('unknown OBJ material: ' + str(label))
        m = materials[label]
        ref = m['map']
        if not ref:
            ref = compiler.missing_texture(name + ':' + str(label))
        seen = set()
        while isinstance(ref, str) and ref.startswith('#'):
            if ref in seen or len(seen) >= 64:
                raise error('cyclic OBJ texture references')
            seen.add(ref)
            if ref[1:] not in model.get('textures', {}):
                ref = compiler.missing_texture(name + ':' + ref)
                break
            ref = model['textures'][ref[1:]]
        # NeoForge's dirty-material resolver accepts texture paths with .png / textures/.
        if isinstance(ref, str) and ref.endswith('.png'):
            ref = ref[:-4]
        if isinstance(ref, str):
            if ':' in ref:
                ns, path = ref.split(':', 1)
                if path.startswith('textures/'):
                    ref = ns + ':' + path[9:]
            elif ref.startswith('textures/'):
                ref = ref[9:]
        ref = compiler.texture(ref)
        color = m['color']
        if color != (1., 1., 1., 1.):
            derived = 'mineways:obj/tint_' + hashlib.sha256((ref + repr(color)).encode()).hexdigest()
            if derived not in compiler.textures:
                width, height, pixels = api.decode_png(compiler.textures[ref][0])
                generated = getattr(compiler, 'obj_texture_pixels', {})
                generated = {key: value for key, value in generated.items() if key in compiler.textures}
                if sum(generated.values()) + width * height > MAX_GENERATED_PIXELS:
                    raise error('OBJ generated-texture pixel budget exceeded')
                generated[derived] = width * height
                compiler.obj_texture_pixels = generated
                tinted = bytes(int(channel * color[index % 4]) for index, channel in enumerate(pixels))
                count = width * height
                alpha_sum = sum(tinted[3::4])
                avg = [sum(tinted[i] * tinted[i + 3 - c] for i in range(c, len(tinted), 4)) // max(1, alpha_sum) for c in range(3)]
                compiler.textures[derived] = (api.encode_png(width, height, tinted), avg, alpha_sum // count)
            ref = derived
        if any(m['ambient'][:3]) and model.get('emissive_ambient', True):
            compiler.warnings.add(name + ': OBJ ambient emission is not exported')
        if ie is not None:
            ref = compiler.face_color_texture(ref, {}, {'neoforge_data': {'color': format(key[1], '08x')}}, name)
        resolved[key] = ref
        return ref

    seen_attributes = {'v': 0, 'vt': 0, 'vn': 0}

    def index(value, entries, kind):
        try:
            number = int(value)
        except ValueError as exc:
            raise error('invalid OBJ index') from exc
        actual = number - 1 if number > 0 else seen_attributes[kind] + number
        if number == 0 or actual < 0 or actual >= len(entries):
            raise error('OBJ index out of range')
        return entries[actual]

    # NeoForge stores positive indices until bake, allowing forward references.
    # Negative indices remain relative to attributes present at the face line.
    for pass_index in (0, 1):
        for fields in records(name):
            kind, args = fields[0], fields[1:]
            if kind in ('v', 'vt', 'vn'):
                if pass_index == 1:
                    seen_attributes[kind] += 1
                    continue
                target = positions if kind == 'v' else texcoords if kind == 'vt' else normals
                if len(target) >= MAX_VERTICES:
                    raise error('OBJ vertex attribute limit exceeded')
                if kind == 'v':
                    if len(args) != 3:
                        raise error('OBJ positions require three components')
                    target.append(tuple(v * 16 for v in numbers(args, -coordinate_limit / 16, coordinate_limit / 16)))
                elif kind == 'vt':
                    if len(args) not in (2, 3):
                        raise error('OBJ UVs require two components')
                    values = numbers(args, -17, 17)
                    api.number(values[0], -16, 16)
                    api.number(values[1], -15 if flip else -16, 17 if flip else 16)
                    if len(values) == 3 and values[2] != 0:
                        raise error('OBJ 3D texture coordinates are unsupported')
                    target.append(values[:2])
                else:
                    if len(args) != 3:
                        raise error('invalid OBJ normal')
                    target.append(numbers(args, -1e6, 1e6))
            elif pass_index == 0:
                continue
            elif kind == 'mtllib':
                if len(args) != 1:
                    raise error('OBJ requires one material-library reference')
                if not model.get('mtl_override'):
                    materials = library(args[0])
                    resolved.clear()
            elif kind == 'usemtl':
                material = ' '.join(args)
            elif kind in ('o', 'g'):
                if not args:
                    raise error('OBJ component has no name')
                label = args[0]
                if ie is not None:
                    # BlockModelSplitter recognizes only object names. It ignores g.
                    if kind == 'o':
                        group, part = label, None
                elif kind == 'o' and (object_above_group or not group):
                    object_above_group, group, part = True, label, None
                elif kind == 'g' and not object_above_group:
                    group, part = label, None
                else:
                    part = group + '/' + label
            elif kind == 'f':
                if len(args) not in (3, 4) or len(polygons) >= MAX_POLYGONS:
                    raise error('OBJ needs bounded triangle/quad faces')
                corners = []
                for token in args:
                    parts = token.split('/')
                    if len(parts) not in (2, 3) or (not parts[1] and ie is None):
                        raise error('OBJ faces need position and UV indices')
                    position = transform(index(parts[0], positions, 'v'))
                    if ie is not None:
                        position = ie.position(group, position)
                        for value in position:
                            api.number(value, -coordinate_limit, coordinate_limit)
                    u, v = index(parts[1], texcoords, 'vt') if parts[1] else (0, 0)
                    if len(parts) == 3:
                        index(parts[2], normals, 'vn')
                        uses_normals = True
                    corners.append((*position, 16 * u, 16 * (1-v if flip else v)))
                component_visible = (group in visible_groups and (part is None or part in visible_groups)) if visible_groups is not None else (
                    visibility.get(group, True) and (part is None or visibility.get(part, True)))
                if (ie is not None and (not ie.visible(group) or material is None)) or not component_visible:
                    hidden_faces += 1
                    continue
                def has_area(a, b, c):
                    ab = [b[i] - a[i] for i in range(3)]
                    ac = [c[i] - a[i] for i in range(3)]
                    cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0])
                    return sum(value*value for value in cross) >= 1e-16
                first_area = has_area(*corners[:3])
                second_area = len(corners) == 4 and has_area(corners[0], corners[2], corners[3])
                if not first_area and not second_area:
                    compiler.warnings.add(name + ': degenerate OBJ faces are omitted')
                    continue
                if len(corners) == 4 and not (first_area and second_area):
                    # Minecraft draws the fixed 0-2 diagonal. A collinear first
                    # triangle does not invalidate the second (IE radio tower).
                    # Preserve that triangle's exact source corners and UVs.
                    corners = corners[:3] if first_area else [corners[i] for i in (0, 2, 3)]
                if api.expanded_uv([corner[3:] for corner in corners]):
                    compiler.warnings.add(name + ': ' + api.REPEATED_UV_WARNING)
                polygons.append({'texture': texture(material, group), 'vertices': corners})
            elif kind == 'vc':
                raise error('OBJ vertex colors are unsupported')
            elif kind not in ('s', 'l'):
                raise error('unsupported OBJ record: ' + kind)
    if not polygons:
        if hidden_faces:
            return {'rotation': rotation, 'elements': [], 'polygons': [], 'invisible': True}
        raise error('OBJ has no supported faces')
    if uses_normals:
        compiler.warnings.add(name + ': OBJ uses flat face normals; source vertex normals are not preserved')
    compiler.warnings.add(name + ': static OBJ mesh; animation, ambient shading and neighbor-face culling are not evaluated')
    compiler.element_count += len(polygons)
    if compiler.element_count > api.MAX_COMPILED_ELEMENTS:
        raise error('compiled polygon budget exceeded')
    return {'rotation': rotation, 'elements': [], 'polygons': polygons}

def model_transform(spec, error, coordinate_limit=256):
    """NeoForge TRSR root transform, converted from center-relative to block coordinates.

    TransformationHelper.Deserializer applies its origin before ObjModel.makeQuad
    applies blockCenterToCorner. Keep both offsets, including its default origin1.
    """
    allowed = {'origin', 'translation', 'rotation', 'left_rotation', 'scale', 'right_rotation', 'post-rotation'}
    if not isinstance(spec, dict) or set(spec) - allowed:
        raise error('unsupported OBJ/composite root transform')

    def vector(value, count, limit):
        if not isinstance(value, (tuple, list)) or len(value) != count or any(type(v) not in (int, float) or abs(v) > limit or not math.isfinite(v) for v in value):
            raise error('invalid OBJ transform vector')
        return tuple(float(v) for v in value)

    def rotation(value):
        if isinstance(value, dict):
            if len(value) != 1 or next(iter(value)) not in ('x', 'y', 'z'):
                raise error('invalid OBJ transform axis rotation')
            axis, angle = next(iter(value.items()))
            if type(angle) not in (int, float) or abs(angle) > 360000 or not math.isfinite(angle):
                raise error('invalid OBJ transform angle')
            return [(axis, float(angle))]
        if isinstance(value, list) and value and isinstance(value[0], dict):
            if len(value) > 64 or not all(isinstance(part, dict) for part in value):
                raise error('OBJ transform rotations require at most 64 axis objects')
            return [item for part in value for item in rotation(part)]
        return list(zip('xyz', vector(value, 3, 360000)))

    if ('rotation' in spec and 'left_rotation' in spec) or ('right_rotation' in spec and 'post-rotation' in spec):
        raise error('duplicate OBJ transform rotations')
    left = rotation(spec.get('rotation', spec.get('left_rotation', [0, 0, 0])))
    right = rotation(spec.get('right_rotation', spec.get('post-rotation', [0, 0, 0])))
    translation = vector(spec.get('translation', [0, 0, 0]), 3, 32)
    scale = spec.get('scale', [1, 1, 1])
    if type(scale) in (int, float):
        scale = [scale] * 3
    scale = vector(scale, 3, 64)
    if any(v == 0 for v in scale):
        raise error('singular OBJ transform scale')
    origin = spec.get('origin', 'opposing-corner')
    if isinstance(origin, str):
        if origin not in ('center', 'corner', 'opposing-corner'):
            raise error('invalid OBJ transform origin')
        origin = [{'center': .5, 'corner': 0., 'opposing-corner': 1.}[origin]] * 3
    origin = tuple(v + .5 for v in vector(origin, 3, 16))

    def rotate(point, rotations):
        point = list(point)
        # Quaternion products apply their rightmost rotation first.
        for axis, degrees in reversed(rotations):
            radians = math.radians(degrees % 360)
            sine, cosine = math.sin(radians), math.cos(radians)
            if degrees % 90 == 0:
                sine, cosine = round(sine), round(cosine)
            a, b = {'x': (1, 2), 'y': (2, 0), 'z': (0, 1)}[axis]
            point[a], point[b] = cosine * point[a] - sine * point[b], sine * point[a] + cosine * point[b]
        return point

    def apply(position):
        point = [position[i] / 16 - origin[i] for i in range(3)]
        point = rotate(point, right)
        point = [point[i] * scale[i] for i in range(3)]
        point = rotate(point, left)
        point = tuple((point[i] + translation[i] + origin[i]) * 16 for i in range(3))
        if any(not math.isfinite(v) or v < -coordinate_limit or v > coordinate_limit for v in point):
            raise error('transformed OBJ coordinates exceed supported range')
        return point
    apply.is_identity = not any(translation) and scale == (1., 1., 1.) and not any(angle % 360 for _, angle in left + right)
    return apply
