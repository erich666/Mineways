"""IE 12.4.2 basic_split: bounded cell clipping before block-state rotation.

The shipped BlockModelSplitter 2.0.1 splits X/Z/Y at integer planes, keeps
coplanar faces on the positive side, and clumps outlying cells into the nearest
split_part. Each output part uses local coordinates; it is never a whole model.
"""
import math
import itertools

MAX_PARTS = 256
MAX_FRAGMENTS = 65536
EPS = 16e-5  # The shipped splitter uses 1e-5 in block units.

# Exact master-from-origin offsets from the shipped TemplateMultiblock constructors.
MASTERS = {
    'alloy_smelter': (0,0,1), 'arc_furnace': (2,1,2), 'assembler': (1,1,1),
    'auto_workbench': (1,1,1), 'blast_furnace': (1,1,1), 'blastfurnace_advanced': (1,1,1),
    'bottling_machine': (1,1,0), 'chunk_loader': (1,2,1), 'coke_oven': (1,1,1),
    'crusher': (2,1,1), 'diesel_generator': (1,1,2), 'excavator': (1,1,5),
    'fermenter': (1,1,1), 'lightningrod': (1,1,1), 'metal_press': (1,1,0),
    'mixer': (1,1,1), 'radio_tower': (2,8,2), 'refinery': (2,1,2),
    'sawmill': (2,1,1), 'shelf': (0,0,0), 'silo': (1,0,1),
    'squeezer': (1,1,1), 'tank': (1,0,1),
}
REGISTRY_NAMES = {'blastfurnace_advanced': 'advanced_blast_furnace', 'lightningrod': 'lightning_rod'}


def policy_for(name, parts, error):
    if not isinstance(name, str) or not name.startswith('immersiveengineering:'):
        raise error('IE split offset provider requires a verified model resource')
    key = name.rsplit('/', 1)[-1].removesuffix('_split').removesuffix('_mirrored')
    for suffix in ('_off', '_on'):
        key = key.removesuffix(suffix)
    width = max(p[0] for p in parts) - min(p[0] for p in parts) + 1
    if key in MASTERS:
        return ('position', 'immersiveengineering:' + REGISTRY_NAMES.get(key, key) + '_master', *MASTERS[key], width)
    simple = {
        'cloche': ('dummy','cloche'), 'core_drill': ('dummy','sampledrill'),
        'blastfurnace_preheater': ('dummy','blastfurnacepreheater'),
        'transformer_hv_left': ('dummy','transformerhv'), 'transformer_mv_left': ('dummy','transformer'),
        'e_meter': ('meter','-'), 'teslacoil': ('slave_z','-'),
        'circuit_table': ('slave_x','-'), 'workbench': ('slave_x','-'),
        'alu_post': ('post','-'), 'steel_post': ('post','-'), 'treated_post': ('post','-'),
    }
    if key not in simple:
        raise error('unsupported IE split offset provider: ' + name)
    policy, entity = simple[key]
    return (policy, '-' if entity == '-' else 'immersiveengineering:' + entity, 0,0,0,width)


def _java_set_order(values, hash_code, error, collection=False):
    """Only the bounded, non-tree HashSet path used by the shipped split parts."""
    capacity = 16
    if collection:
        requested = max(16, int(len(values)/.75)+1)
        while capacity < requested: capacity *= 2
    buckets = [[] for _ in range(capacity)]
    count = 0
    for value in values:
        h = hash_code(value) & 0xffffffff
        h ^= h >> 16
        bucket = buckets[h & (capacity-1)]
        if any(v == value for _,v in bucket): continue
        bucket.append((h,value)); count += 1
        collision = len(bucket) >= 9
        if collision and capacity >= 64:
            raise error('IE split part HashSet tree collision is unsupported')
        if count > capacity*.75 or collision:
            capacity *= 2
            grown = [[] for _ in range(capacity)]
            for old in buckets:
                for key,v in old: grown[key & (capacity-1)].append((key,v))
            buckets = grown
    return [v for bucket in buckets for _,v in bucket]


def clump_order(parts, error):
    # UnbakedSplitModel first constructs HashSet<BlockPos>(parts). AbstractSplitModel
    # then collects mapped ModelSplitterVec3i records into a fresh HashSet.
    positions = _java_set_order(parts, lambda p:(p[1]+p[2]*31)*31+p[0], error, True)
    return _java_set_order(positions, lambda p:(p[0]*31+p[1])*31+p[2], error)


def _clip(vertices, axis, plane, positive):
    # Polygon.splitAlong keeps a face whole if it has only one nonzero sign.
    # Epsilon-zero endpoints alone must not fabricate an opposite-side sliver.
    plus = any(v[axis] - plane > EPS for v in vertices)
    minus = any(v[axis] - plane < -EPS for v in vertices)
    if not (plus and minus):
        return vertices if positive == (not minus) else []
    result = []
    for a, b in zip(vertices, vertices[1:] + vertices[:1]):
        da, db = a[axis] - plane, b[axis] - plane
        inside_a = da >= -EPS if positive else da <= EPS
        inside_b = db >= -EPS if positive else db <= EPS
        if inside_a:
            result.append(a)
        if inside_a != inside_b and abs(db-da) > EPS:
            t = -da/(db-da)
            result.append(tuple(a[i] + t*(b[i]-a[i]) for i in range(5)))
    clean = []
    for vertex in result:
        if not clean or max(abs(vertex[i]-clean[-1][i]) for i in range(3)) > EPS:
            clean.append(vertex)
    if len(clean) > 1 and max(abs(clean[0][i]-clean[-1][i]) for i in range(3)) <= EPS:
        clean.pop()
    return clean


def _cross_squared(a, b, c):
    cross = tuple((b[(i+1)%3]-a[(i+1)%3])*(c[(i+2)%3]-a[(i+2)%3]) -
                  (b[(i+2)%3]-a[(i+2)%3])*(c[(i+1)%3]-a[(i+1)%3]) for i in range(3))
    return sum(value*value for value in cross)


def _affine_uv(vertices):
    # A different diagonal can change non-affine UV interpolation. Fit both UV
    # components to a well-conditioned projected triangle and check every corner.
    ids = max(itertools.combinations(range(len(vertices)), 3),
              key=lambda ids:_cross_squared(*(vertices[i] for i in ids)))
    a,b,c = (vertices[i] for i in ids)
    edge1 = [b[i]-a[i] for i in range(3)]
    edge2 = [c[i]-a[i] for i in range(3)]
    normal = [edge1[(i+1)%3]*edge2[(i+2)%3]-edge1[(i+2)%3]*edge2[(i+1)%3] for i in range(3)]
    drop = max(range(3), key=lambda i:abs(normal[i]))
    i,j = [axis for axis in range(3) if axis != drop]
    determinant = edge1[i]*edge2[j]-edge1[j]*edge2[i]
    if determinant == 0:
        return False
    for v in vertices:
        x,y = v[i]-a[i],v[j]-a[j]
        first = (x*edge2[j]-y*edge2[i])/determinant
        second = (edge1[i]*y-edge1[j]*x)/determinant
        if any(abs(v[k]-(a[k]+first*(b[k]-a[k])+second*(c[k]-a[k]))) > 1e-8 for k in (3,4)):
            return False
    return True


def _stable_fan(vertices):
    # Clipping a convex source face can add nearly collinear corners. A fan
    # anchored at the first corner can isolate an avoidable subprecision triangle.
    # Select the fan with the strongest weakest triangle, preserving every vertex,
    # UV pair, edge, winding and the covered polygon. Triangles/quads stay unchanged.
    if len(vertices) <= 4:
        return [vertices]
    def cross(a, b, c):
        return tuple((b[(i+1)%3]-a[(i+1)%3])*(c[(i+2)%3]-a[(i+2)%3]) -
                     (b[(i+2)%3]-a[(i+2)%3])*(c[(i+1)%3]-a[(i+1)%3]) for i in range(3))
    normals = [cross(vertices[i-1], vertices[i], vertices[(i+1)%len(vertices)]) for i in range(len(vertices))]
    normal = max(normals, key=lambda n:sum(v*v for v in n))
    length2 = sum(v*v for v in normal)
    convex = length2 > 0 and all(sum(a*b for a,b in zip(n,normal)) >= -length2*1e-12 for n in normals)
    planar = all(abs(sum((v[i]-vertices[0][i])*normal[i] for i in range(3))) <= EPS*math.sqrt(length2) for v in vertices)
    if not convex or not planar or not _affine_uv(vertices):
        # Retain the established tessellation if geometry or UVs prevent equivalence.
        return [[vertices[0], vertices[i], vertices[i+1]] for i in range(1,len(vertices)-1)]
    def strength(anchor):
        points = vertices[anchor:] + vertices[:anchor]
        return min(_cross_squared(points[0], points[i], points[i+1]) for i in range(1, len(points)-1))
    anchor = max(range(len(vertices)), key=strength)
    points = vertices[anchor:] + vertices[:anchor]
    return [[points[0], points[i], points[i+1]] for i in range(1, len(points)-1)]


def split_polygons(polygons, parts, error, warnings=None):
    if not isinstance(parts, list) or not 1 <= len(parts) <= MAX_PARTS:
        raise error('IE split requires 1..256 declared parts')
    if any(not isinstance(p, (tuple,list)) or len(p) != 3 or any(type(v) is not int or not -16 <= v <= 16 for v in p) for p in parts):
        raise error('IE split part coordinates must be integer triples in [-16,16]')
    parts = [tuple(p) for p in parts]
    if len(set(parts)) != len(parts):
        raise error('duplicate IE split part')
    ownership_order = clump_order(parts, error)
    result = {p: [] for p in parts}
    fragments = 0
    for polygon in polygons:
        vertices = [tuple(v) for v in polygon['vertices']]
        if len(vertices) not in (3,4) or any(len(v) != 5 or any(not math.isfinite(c) for c in v) or any(abs(c)>4096 for c in v[:3]) for v in vertices):
            raise error('invalid IE split source polygon')
        cells = [(vertices, [0,0,0])]
        for axis in (0,2,1):
            next_cells = []
            for verts, cell in cells:
                low, high = min(v[axis] for v in verts), max(v[axis] for v in verts)
                first, last = math.floor((low+EPS)/16), math.floor((high+EPS)/16)
                if last-first > 64:
                    raise error('IE split source spans more than 64 cells on one axis')
                remaining = verts
                for cell_axis in range(first, last+1):
                    plane = (cell_axis+1)*16
                    # Entirely coplanar geometry belongs only to the positive cell.
                    piece = _clip(remaining, axis, plane, False) if cell_axis < last else remaining
                    if len(piece) >= 3 and max(v[axis] for v in remaining) > low-EPS:
                        if not all(abs(v[axis]-plane) <= EPS for v in piece):
                            key = cell.copy(); key[axis] = cell_axis
                            next_cells.append((piece,key))
                    if cell_axis < last:
                        remaining = _clip(remaining, axis, plane, True)
                        if len(remaining) < 3: break
            cells = next_cells
            if len(cells)+fragments > MAX_FRAGMENTS:
                raise error('IE split fragment budget exceeded')
        for verts, cell in cells:
            key = tuple(cell)
            if key not in result:
                distances = [sum((a-b)**2 for a,b in zip(p,cell)) for p in ownership_order]
                best = min(distances)
                candidates = [p for p,d in zip(ownership_order,distances) if d == best]
                key = candidates[0]
            local = [tuple(v[i]-key[i]*16 if i<3 else v[i] for i in range(5)) for v in verts]
            if any(abs(c)>256 for v in local for c in v[:3]):
                raise error('IE clumped part exceeds supported local geometry bounds')
            pieces = _stable_fan(local)
            for piece in pieces:
                if _cross_squared(*piece[:3]) <= 1e-16: continue
                result[key].append({'texture':polygon['texture'],'vertices':piece})
                fragments += 1
                if fragments > MAX_FRAGMENTS or len(result[key]) > 16384:
                    raise error('IE split polygon budget exceeded')
    return result


def compile_split(compiler, name, model, rotation, selector, depth, error):
    if type(model.get('dynamic')) is not bool:
        raise error('IE split dynamic must be boolean')
    parts = model.get('split_parts')
    # Validate before parsing expensive inner OBJ resources.
    split_polygons([], parts, error)
    policy = policy_for(name, parts, error)
    inner = model.get('inner_model')
    if not isinstance(inner, dict): raise error('IE split inner_model must be an object')
    mirrored = inner.get('loader') == 'immersiveengineering:mirror'
    if mirrored:
        inner = inner.get('inner_model')
        if not isinstance(inner, dict): raise error('IE mirrored inner_model must be an object')
    compiled = compiler.compile_model({'model':inner}, depth+1, selector)
    if compiled.get('elements') or compiled.get('split_models'):
        raise error('IE split inner model must compile to polygon geometry')
    polygons = compiled.get('polygons', [])
    if mirrored:
        polygons = [{'texture':p['texture'],'vertices':[(16-v[0],*v[1:]) for v in reversed(p['vertices'])]} for p in polygons]
    cells = split_polygons(polygons, parts, error, compiler.warnings)
    compiler.warnings.add(str(name) + ': IE cell geometry uses saved model offsets; missing required block-entity context stops rendering')
    return {'rotation':rotation,'elements':[],'split_models':[
        {'rotation':rotation,'elements':[],'polygons':faces,'part':part,'split_policy':policy}
        for part,faces in cells.items()]}
