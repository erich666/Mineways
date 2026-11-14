# Open the ATM10 showcase

This showcase contains 24 sample blocks from ATM10 8.3 for Minecraft 1.21.1. It opens in Mineways with the matching textures and export settings. It is not a game-playable Minecraft world.

## First launch

1. Download the Windows or universal macOS preview ZIP from the [releases page](https://github.com/wbreiler/Mineways/releases).
2. Extract the complete ZIP into a writable folder.
3. Double-click the file for your platform below.

| Platform | File to double-click |
| --- | --- |
| Windows x64 | `Open ATM10 Showcase.cmd` |
| Apple Silicon or Intel, macOS 26 or later | `Open ATM10 Showcase.command` |

No manual Python or Minecraft installation is required. The first setup downloads approximately 30 MB for its Python runtime. It then downloads 12 verified files, totaling 105.5 MB, for the sample blocks. Allow additional disk space for the prepared textures.

Keep the setup window open until Mineways opens. Setup loads the matching pack before the showcase world. It selects the samples and enables the detailed export settings.

If macOS blocks the app or setup file, follow **Download and open** in `QUICKSTART.md`. The app is not notarized.

The downloads remain on your computer. The preview ZIP does not contain third-party JARs or their textures. Setup does not run mod code.

## Explore the samples

The four rows contain six blocks each. All samples sit at Y `66`, above colored pedestals.

| Row | Z coordinate | Examples |
| --- | --- | --- |
| Materials | `2` | Fluix, quartz, osmium, steel, fluorite, and canopy planks |
| Machines | `6` | Charger, inscriber, controller, energy cells, and capacitor |
| Glass and shapes | `9` | Quartz glass, spatial pylon, firefly jar, stairs, fence, and lantern |
| Workshop | `13` | Chest, stove, canvas sign, buffer, item pipe, and cable |

The selected region spans X `0`–`15`, Y `64`–`67`, and Z `0`–`15`.

## Export the showcase

1. Select **File > Export for Rendering** on Windows, or **File > Export Model** on macOS.
2. Select Wavefront OBJ and the individual texture mode.
3. Export into a new folder.

Repeat with **Export full color texture patterns** on Windows, or **Full (mosaic) textures** on macOS. Both modes should preserve the sample geometry.

Keep the OBJ, MTL, and texture files together. Open the OBJ in a 3D viewer with materials enabled. The Mineways map shows block colors rather than the complete exported geometry.

Use Sponge `.schem` for schematic exports. The legacy `.schematic` format cannot preserve mod block identities.

## If setup fails

Read the error message before you close the setup window. Correct the reported network, disk, or permission problem. Then double-click the same setup file again.

Keep the extracted package together. Do not move individual manifests or texture folders.

These samples use fixed mod versions. They do not represent every ATM10 installation. Unsupported blocks and visual approximations remain possible in other worlds.

For feedback, include the preview filename, operating system, sample name, screenshot, and setup or export log.
