"""Static EnderIO components verified from the shipped 8.2.12-beta classes."""

IO_OVERLAY_LOADER = 'enderio:io_overlay'


def compile_io_overlay(compiler, name, rotation):
    """No IO_CONFIG_PROPERTY means no quads, regardless of model transforms.

    IOOverlayBakedModel.Loader ignores JSON geometry. The machine's ordinary
    geometry lives in a separate composite child and must remain visible.
    """
    label = name if isinstance(name, str) else '<inline>'
    compiler.warnings.add(f'{label}: EnderIO IO configuration overlay omitted; its visible settings require block-entity data')
    return {'rotation': rotation, 'elements': [], 'invisible': True}
