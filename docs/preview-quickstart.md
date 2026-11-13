# Mod support preview quickstart

This independent Mineways fork provides experimental mod support on Windows and macOS. This preview is not an upstream release.

## Download and open

Use the mod support preview on the [fork releases page](https://github.com/wbreiler/Mineways/releases). The older `v14.03` release predates mod support.

Choose the ZIP for your platform. Extract the complete ZIP into a writable folder. Keep its files together.

- `windows-x64`: Windows x64, with `Mineways.exe` at the package root.
- `macos-universal`: macOS 26 or later, with `Mineways.app` at the package root.

The single macOS package runs natively on Apple Silicon and Intel Macs. Rosetta is not required.

Each macOS package uses an ad-hoc signature. It is not notarized. The release notes identify the tested architectures.

If macOS rejects the downloaded app as damaged, remove its quarantine attribute after you verify the download source:

```sh
xattr -cr "/path/to/Mineways-preview-macos-universal/Mineways.app"
```

Use your extracted app path. Then open `Mineways.app`. On Windows, open `Mineways.exe`.

If Windows reports that it cannot find a Minecraft saves folder, select **OK**. The included demo still works. Follow the load order below.

`BUILD-METADATA.json` identifies the source commit and platform. `SHA256SUMS` lists package file hashes.

## Load the included demo

The demo requires no Python, Minecraft installation, mod JARs, or external downloads. Its original assets are included for redistribution.

Load the pack **before** the world or schematic. A pack change while a schematic is open fails. Restart Mineways before this procedure if necessary.

1. Select **File > Load Mod Pack**.
2. Open `demo/manifest.mwpack` from the extracted package.
3. Open `demo/world/level.dat` through the File menu's world or schematic command.

Select the gallery:

1. Select **File > Go To Location**.
2. Enter X `8` and Z `8`.
3. Set the vertical controls as shown below.
4. Drag with the right mouse button to select the gallery.

| Platform | Upper limit | Lower limit |
| --- | --- | --- |
| Windows | Height: `67` | Depth: `64` |
| macOS | Depth: `67` | Bottom: `64` |

The exact export bounds are X `0` through `15`, Y `64` through `67`, and Z `0` through `15`. Adjust the export dialog's coordinate fields to these bounds.

The gallery contains eight isolated blocks above a stone floor:

- A yellow cube and a teal cube.
- A transparent purple cube.
- Two blue arrow signs, with north and east orientations.
- Three copper sensors: a short base, a tall multipart model, and a rotated multipart model.

The map shows block colors. Inspect the exported OBJ to check geometry, arrow orientation, and transparency.

## Export an OBJ in both texture modes

On Windows, use **File > Export for Rendering**. On macOS, use **File > Export Model** and **Load Rendering Defaults**.

1. Choose the Wavefront OBJ format.
2. Set the selection bounds to the gallery coordinates above.
3. Enable **Export lesser blocks** on Windows or **Export precise/detailed partial blocks** on macOS.
4. Select the individual texture mode from the table below.
5. Save the export as `gallery-individual.obj` in a new output folder.

Repeat with mosaic textures:

1. Open the export dialog again.
2. Select the mosaic texture mode from the table below.
3. Save the export as `gallery-mosaic.obj` in a separate output folder.

| Mode | Windows option | macOS option |
| --- | --- | --- |
| Individual textures | Export individual textures | Individual tile textures (per block face) |
| Mosaic textures | Export full color texture patterns | Full (mosaic) textures |

Keep each OBJ with its MTL and texture files. Import the OBJ into your 3D viewer with materials enabled. Both exports should show the same gallery geometry and colors.

## Test the custom dimension

1. Keep the demo world open.
2. Select **View > Dimensions**.
3. Choose `mineways_demo:gallery`.
4. Return to X `8`, Z `8`.
5. Select the same gallery bounds.

Export another OBJ from this dimension.

The custom dimension repeats the gallery. Its block identities, geometry, and textures should match the overworld gallery.

For a schematic test, open `demo/demo.schem` after the demo pack. Its local bounds are X `0`–`15`, Y `0`–`3`, Z `0`–`15`.

Use Sponge `.schem` for mod schematic exports. The legacy `.schematic` format cannot preserve mod block identities.

## Try your own pack

Only custom pack compilation requires Python 3.9 or later. No extra Python packages are required. Keep the bundled `tools/mod_*.py` helpers beside `tools/import_mod_assets.py`.

```sh
python3 tools/import_mod_assets.py \
  /path/to/minecraft-client.jar \
  /path/to/example-mod.jar \
  --output /path/to/new-pack
```

On Windows, use `python` or `py -3` instead of `python3`. The output folder must not exist. Use matching client, loader, mod, and resource assets for your world.

Read the generated `report.json`. Load the generated `manifest.mwpack` before your world. Keep its `textures/` folder beside the manifest.

See [mod support documentation](mod-support.md) for source priority, large pack commands, supported models, and limits.

## Expected omissions

Support depends on the supplied assets and each block's renderer. A successful pack compilation does not mean every block or state is supported.

- Unsupported blocks or states can appear as the unknown-block fallback. Sponge imports can replace unsupported states with air.
- Unsupported states cannot retain their original identity through schematic export.
- Arbitrary Java renderers, unsupported custom loaders, and arbitrary block-entity data remain unsupported.
- Some supported adapters approximate connections, inventories, activity, dynamic tint, or other runtime details.
- Weighted models use the first alternative. Animated textures use a static frame.
- Albedo textures are supported. Custom biome colors use a fallback tint. Shader and PBR effects are not reproduced.

The included original demo does not establish compatibility with every modpack. The importer report identifies pack-specific omissions and approximations.

## Report a problem

Include this information in an issue on the [fork issue tracker](https://github.com/wbreiler/Mineways/issues):

```text
Preview asset filename:
Build source SHA (BUILD-METADATA.json):
Operating system and CPU architecture:
Minecraft version:
Modpack name and version:
Mod name and version:
Compiled pack version or source asset list:
World or schematic:
Dimension ID:
Coordinates (X, Y, Z) and selection bounds:
Block ID and full blockstate properties:
Export format and texture mode:
Expected result:
Actual result:
Reproduction steps:
Screenshot and relevant warning/log text:
Importer report.json entries, if applicable:
```

Include the smallest shareable reproduction. Do not attach private worlds or third-party assets without permission.
