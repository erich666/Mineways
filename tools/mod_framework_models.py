"""Framework 0.13.11 OpenModelDeserializer's static element interpretation."""

LOADER = 'framework:open_model'
_ZERO_ENDPOINT_UV = {
    'up': (0, 0, 0, 0), 'down': (0, 16, 0, 16),
    'north': (16, 16, 16, 16), 'south': (0, 16, 0, 16),
    'west': (0, 16, 0, 16), 'east': (16, 16, 16, 16),
}


def adapt_open_model(model, error_type, max_elements=2048):
    """Restore unrestricted components after Framework's zero-endpoint parse.

    The supplied wrapper only exposes JSON data; it does not alter baked quads.
    Native shared element compilation still enforces coordinates, angles, faces,
    texture paths, UV limits, and budgets. Do not mutate cached parent models.
    """
    if model.get('transform', {}) != {} or model.get('visibility', {}) != {}:
        raise error_type('Framework root transforms and visibility require explicit replacement models')
    components = model.get('components')
    if not isinstance(components, list) or not components or len(components) > max_elements:
        raise error_type('Framework model requires bounded nonempty components')
    elements = []
    for component in components:
        if not isinstance(component, dict):
            raise error_type('invalid Framework component')
        element = dict(component)
        rotation = component.get('rotation', {})
        if not isinstance(rotation, dict):
            raise error_type('invalid Framework component rotation')
        if rotation.get('rescale', False) and abs_angle(rotation.get('angle', 0)) not in (22.5, 45):
            raise error_type('nonstandard Framework rotation with rescale is unsupported')
        faces = component.get('faces')
        if not isinstance(faces, dict) or not faces:
            raise error_type('Framework component requires faces')
        element['faces'] = {}
        for side, face in faces.items():
            if side not in _ZERO_ENDPOINT_UV or not isinstance(face, dict):
                raise error_type('invalid Framework component face')
            element['faces'][side] = dict(face)
            if 'uv' not in face:
                element['faces'][side]['uv'] = list(_ZERO_ENDPOINT_UV[side])
        # Framework copies the parsed faces/shade into a fresh BlockElement but
        # omits its extra element metadata. Face-level extra metadata survives.
        element.pop('neoforge_data', None)
        element.pop('forge_data', None)
        elements.append(element)
    return {**{key: value for key, value in model.items()
               if key not in ('loader', 'parent', 'components', 'elements', 'data')},
            'elements': elements}


def abs_angle(value):
    # Shared compilation rejects nonnumeric, nonfinite, and out-of-bound angles.
    return abs(value) if type(value) in (int, float) else None
