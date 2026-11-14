"""Supplementaries 4.0.1 faucet body when saved FLUID model data is absent.

Moonlight's GeometryWrapper passes ModelState, but no outer geometry context,
into both children. FaucetBakedModel emits only the body without FLUID data.
"""
LOADER = 'supplementaries:faucet'
FIELDS = ('model', 'liquid')


def compile_faucet(compiler, name, model, rotation, selector, depth, error_type, block_name=None):
    if depth >= 63:
        raise error_type('excessive faucet model depth')
    if model.get('loader') != LOADER:
        raise error_type('unsupported faucet loader')
    children = []
    for field in FIELDS:
        source = model.get(field)
        if not isinstance(source, (str, dict)):
            raise error_type('faucet requires a resource or inline model for ' + field)
        resolved = compiler.model(source)
        # Standard element compilation does not apply root transforms. The
        # OBJ/composite paths validate and apply their supported transforms.
        if (resolved.get('transform', {}) != {}
                and resolved.get('loader') not in ('neoforge:obj', 'neoforge:composite', 'forge:composite')):
            raise error_type('faucet child root transform requires an explicit replacement model')
        application = {'model': source, 'x': rotation[0], 'y': rotation[1], 'uvlock': bool(rotation[2])}
        if len(rotation) == 4:
            application['ae2:z'] = rotation[3]
        child = compiler.compile_model(application, depth + 1, selector, block_name)
        if 'split_models' in child or 'split_policy' in child:
            raise error_type('nested split faucet child requires an explicit replacement model')
        children.append(child)
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(label + ': dry faucet body; saved fluid identity, color, emission and flowing stream are not evaluated')
    if model.get('transform', {}) or model.get('textures') or model.get('visibility'):
        compiler.warnings.add(label + ': outer faucet textures, visibility and root transforms are ignored by the shipped loader; replace its child models instead')
    # The liquid still bakes and consumes the shared budget, but contributes no
    # exported faces. Serialization includes only textures referenced by faces.
    # Retain body cullfaces, fixed rotation, and explicit invisible requirements.
    return children[0]
