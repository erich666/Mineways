"""Amendments 2.1.11 wall lanterns, using its verified static fast-renderer pose.

WallLanternBlock derives the mimic state from LanternType, with hanging=false.
WallLanternModelsManager selects a legacy custom body, then the current custom
body, then the original blockstate. WallLanternBakedModel transforms that body
about the block center by application * translate(0,2,2) * Java Y+90 degrees.
"""
import copy

LOADER = 'amendments:wall_lantern'
# Verified shipped registration names. Do not infer arbitrary LanternType IDs:
# Moonlight drops directory prefixes when it constructs the variant block name.
ORIGINALS = {
    'amendments:wall_lantern': 'minecraft:lantern',
    'amendments:wall_soul_lantern': 'minecraft:soul_lantern',
    'amendments:forbidden_arcanus/wall_deorum_lantern': 'forbidden_arcanus:deorum_lantern',
    'amendments:forbidden_arcanus/wall_deorum_soul_lantern': 'forbidden_arcanus:deorum_soul_lantern',
    'amendments:architects_palette/wall_nether_brass': 'architects_palette:nether_brass',
    'amendments:byg/wall_cryptic_lantern': 'byg:cryptic_lantern',
    'amendments:byg/wall_glowstone_lantern': 'byg:glowstone_lantern',
    'amendments:oxidized/wall_copper_lantern': 'oxidized:copper_lantern',
    'amendments:pokecube_legends/wall_infected_lantern': 'pokecube_legends:infected_lantern',
    **{'amendments:twigs/wall_' + name: 'twigs:' + name for name in (
        'allium_paper_lantern', 'blue_orchid_paper_lantern', 'crimson_roots_paper_lantern',
        'dandelion_paper_lantern', 'paper_lantern')},
}


def _asset(name, kind):
    namespace, path = name.split(':', 1)
    return 'assets/' + namespace + '/' + kind + '/' + path + '.json'


def _body_application(compiler, block_name, selector, error_type):
    original = ORIGINALS.get(block_name)
    if original is None:
        raise error_type('unverified wall lantern original block mapping: ' + str(block_name))
    namespace, path = original.split(':', 1)
    prefix = '' if namespace in ('minecraft', 'amendments') else namespace + '/'
    candidates = ('amendments:block/custom_wall_lanterns/' + prefix + path,
                  'amendments:block/' + block_name.split(':', 1)[1] + '_lantern')
    for candidate in candidates:
        if _asset(candidate, 'models') in compiler.assets.files:
            return {'model': candidate}
    source = _asset(original, 'blockstates')
    if source not in compiler.assets.files:
        raise error_type('missing wall lantern original blockstate: ' + original)
    definition = compiler.assets.json(source)
    variants = definition.get('variants')
    if not isinstance(variants, dict) or not variants or len(variants) > 4096:
        raise error_type('wall lantern original body requires bounded variants: ' + original)
    known = {'hanging': 'false'}
    if 'lit' in (selector or {}):
        known['lit'] = selector['lit']
    matches = []
    for key, value in variants.items():
        properties = {}
        if key:
            for pair in key.split(','):
                pieces = pair.split('=')
                if len(pieces) != 2 or not all(pieces) or pieces[0] in properties:
                    raise error_type('invalid wall lantern original variant selector: ' + original)
                properties[pieces[0]] = pieces[1]
        if any(prop not in known for prop in properties):
            raise error_type('wall lantern original body has unverified default properties: ' + original)
        if all(known[prop] == value for prop, value in properties.items()):
            matches.append(value)
    if len(matches) != 1:
        raise error_type('wall lantern original body needs one matching default variant: ' + original)
    return matches[0]


def compile_wall_lantern(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    x, y, uvlock = rotation[:3]
    if x or (len(rotation) == 4 and rotation[3]) or model.get('transform', {}) != {}:
        raise error_type('wall lantern requires a Y-only application without a root transform')
    support = model.get('support')
    if not isinstance(support, (str, dict)):
        raise error_type('wall lantern support must be a model resource or inline model')
    application = _body_application(compiler, block_name, selector, error_type)
    body = compiler.compile_model(application, depth + 1, selector, block_name)
    chosen = application[0] if isinstance(application, list) else application
    source_model = compiler.model(chosen['model'])
    if (source_model.get('transform', {}) != {}
            and source_model.get('loader') not in ('neoforge:obj', 'neoforge:composite', 'forge:composite')):
        raise error_type('wall lantern body has an unsupported root transform')
    if (body['rotation'][0] or body['rotation'][1]
            or (len(body['rotation']) == 4 and body['rotation'][3])
            or 'split_models' in body or 'split_policy' in body):
        raise error_type('wall lantern body requires an unrotated model without a split provider')
    if not body.get('elements') and not body.get('polygons'):
        raise error_type('wall lantern body has no visible geometry')
    # Translate after compilation so implicit UVs are computed from the original
    # source coordinates. T(0,2,2)*Ry(+90) == Ry(+90)*T(-2,2,0).
    body = copy.deepcopy(body)
    shift = (-2, 2, 0)
    def translated(point):
        result = tuple(value + offset for value, offset in zip(point, shift))
        if any(not -256 <= value <= 256 for value in result):
            raise error_type('translated wall lantern body exceeds model coordinate bounds')
        return result
    for element in body['elements']:
        coords = element['coordinates']
        element['coordinates'] = (*translated(coords[:3]), *translated(coords[3:6]),
                                  *coords[6:8], *translated(coords[8:11]), coords[11])
    for polygon in body.get('polygons', []):
        polygon['vertices'] = [(*translated(vertex[:3]), *vertex[3:]) for vertex in polygon['vertices']]
    body['rotation'] = (0, (y + 270) % 360, 0)
    bracket = compiler.compile_model({'model': support, 'y': y, 'uvlock': bool(uvlock)},
                                     depth + 1, selector, block_name)
    if 'split_models' in bracket or 'split_policy' in bracket:
        raise error_type('wall lantern support cannot use a split provider')
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(label + ': complete wall lantern uses the static fast-renderer pose; animated swing and fancy renderer behavior are not evaluated')
    return {'rotation': rotation, 'elements': [], 'split_models': [bracket, body]}
