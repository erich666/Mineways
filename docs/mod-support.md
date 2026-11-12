# Mod support packs

Mineways can load a compiled pack of mod block definitions, textures, and dimension metadata. The importer reads asset folders, JAR files, and ZIP files. It does not run or decompile Java code.

## Create a pack

Use Python 3.9 or later. No Python packages are required. On Windows, use `python` or `py -3` instead of `python3`.

```sh
python3 tools/import_mod_assets.py \
  /path/to/minecraft-client.jar \
  /path/to/example-mod.jar \
  /path/to/resource-pack.zip \
  --output /path/to/new-mineways-pack
```

Sources appear in priority order. Later sources replace earlier files with the same resource path. The output directory must not exist. The importer never overwrites a previous pack.

The Minecraft client JAR supplies vanilla parent models and textures. It is optional when a mod uses only its own assets or the built-in cube templates. The importer provides `cube`, `cube_all`, `cube_column`, and `cube_bottom_top` templates.

For extracted assets, select the folder that contains `assets/`, or select the `assets/` folder itself. Select each JAR separately when a launcher pack contains several mod archives. The importer does not recursively open nested archives or download files from a launcher manifest.

By default, the importer selects all non-`minecraft` block and dimension namespaces. Use `--namespace examplemod` to select one namespace. Repeat that option to select several namespaces. Vanilla assets can supply model parents and textures, but this feature does not replace vanilla block definitions.

The output contains:

- `manifest.mwpack`: the compiled, versioned block definitions.
- `textures/`: the PNG files required by those definitions.
- `report.json`: imported counts, unsupported blocks, and approximation warnings.

Read `report.json` before use. A successful import can include unsupported blocks. Mineways treats those blocks as unknown. A pack with no supported blocks or dimensions fails without creating output.

## Load a pack

Use **File > Load Mod Pack** on Windows or macOS and select `manifest.mwpack`. Use **Clear Mod Pack** to remove the active pack. The script equivalents are:

```text
Mod pack: /path/to/new-mineways-pack/manifest.mwpack
Minecraft world: /path/to/world
```

```text
Clear mod pack
```

Keep the manifest and its `textures` directory together. A pack is local data. The runtime does not execute its contents or fetch remote resources. One compiled pack is active at a time. Prepare different combinations of mods and resource overrides as separate compiled packs.

The importer preserves full namespaced identifiers. For example, `mod_a:marble` and `mod_b:marble` remain distinct. Model selectors match their listed properties, while the world reader retains additional properties for schematic export. A world or schematic must use the corresponding pack when it is loaded. Use Sponge `.schem` for mod exports. The legacy `.schematic` format cannot preserve these block identities, so Mineways rejects mod blocks for that format.

## Dimensions

The importer reads `data/<namespace>/dimension/*.json`. A dimension can use an inline type or reference `data/<namespace>/dimension_type/*.json`. Complete type metadata supplies `min_y`, `height`, and `coordinate_scale`.

Incomplete dimension types appear in `report.json`. The application can still discover their world directories without a pack. Discovery uses available chunk heights and default coordinate scale when pack metadata is absent. A compiled pack can contain dimensions without block definitions.

## Supported models

The importer resolves standard JSON model parents and texture aliases. It supports box elements, face UVs, face rotations, element rotations, element rescale, blockstate X/Y rotations, and UV locking.

Blockstate variants select a model. Multipart conditions expand into bounded property combinations. Boolean conditions include both `true` and `false`. Unconditional multipart geometry supplies a fallback for property values absent from the model definitions.

The current implementation has these explicit limits:

- A state with no matching model selector uses the unknown-block fallback (air by default). Chunk loading reports its namespaced state and continues. This includes modern palettes that omit default properties when a mod provides only explicit property selectors: JSON assets do not reveal Java-defined defaults, so Mineways does not guess them. Sponge imports also use air for these states. Unsupported states cannot retain their identity when exported.
- Weighted alternatives use the first listed model. Animated textures use the first square frame. The report lists these approximations.
- Custom Java renderers, custom model loaders, inverted elements, and UV coordinates outside 0–16 are unsupported. Biome and custom tints are reported but not applied.
- PNG import supports non-interlaced 8-bit grayscale, RGB, indexed, grayscale-alpha, and RGBA images. The output always uses RGBA.
- Imported textures use vacant atlas cells and are resampled to the selected terrain tile resolution. This release has 915 vacant cells. The importer rejects larger packs before writing output, and the runtime checks capacity again. Select fewer mods or namespaces when the pack exceeds this limit. Only albedo textures are imported.
- The runtime has a finite 12-bit block type space and supports at most 4096 observed states per imported block. Exceeding a limit must produce an error.

The importer limits input files, decompressed asset bytes, PNG pixels, model parent depth, multipart combinations, and compiled elements. It rejects unsafe archive paths, duplicate archive entries, archive symlinks, and source symlinks. The runtime separately validates the compiled manifest and texture paths.

## Test without a Minecraft installation

```sh
python3 Mac/tests/mod_assets.py
python3 Mac/tests/mod_assets.py --fixture /tmp/mineways-mod-fixture
```

The fixture creates original test assets and `/tmp/mineways-mod-fixture/pack/manifest.mwpack`. It includes a cube with six distinct texture gradients, a rotated non-cube, and a transparent flat panel. These assets can test UV orientation, model transforms, transparency, and schematic state preservation without third-party resources.

## Native build checks

The macOS `make -C Mac test-mods` target checks the importer, registry, and real export paths. The Windows CI workflow uses MSBuild for Debug and Release builds. Its mod test runner exercises the resulting native binaries. CI runs those checks on their respective operating systems. A local macOS run does not establish a Windows test result.

## Test an installed mod pack

Compile the assets into a pack first. The optional native smoke test selects actual model states and creates temporary exports. It does not open Minecraft or modify a saved world.

```sh
make -C Mac test-real-mods \
  MOD_PACK=/path/to/compiled-pack/manifest.mwpack
```

This target compiles the shared native core with AddressSanitizer and UndefinedBehaviorSanitizer. It checks finite OBJ coordinates and valid face indices, exported texture pixels, repeat exports, and exact Sponge state and geometry round trips. It prints the temporary artifact directory. `selected-states.txt` lists the tested states. Model sampling is deterministic and does not validate every selector or provide a visual comparison with Minecraft.

On November 12, 2026, this test passed on macOS with assets from the installed ATM10 8.3 instance and its Minecraft 1.21.1 client:

| Imported subset | Loaded blocks / selectors / textures | Tested states | Exported faces | Pixel-checked textures |
| --- | --- | --- | --- | --- |
| AllTheModium 3.2.1 | 99 / 993 / 95 | 78 | 784 | 47 |
| AllTheModium 3.2.1, Applied Energistics 2 19.2.18, Iron Furnaces 4.3.2, Simply Light 1.5.3 | 340 / 4192 / 441 | 100 | 1264 | 82 |

Both runs used AddressSanitizer and UndefinedBehaviorSanitizer. The combined pack included three dimension metadata records. These tests checked block exports and did not navigate those dimensions. Its importer report identified 44 unsupported blocks and 64 approximation warnings. This is a tested subset of ATM10. Importing all 495 installed mod JARs exceeded the importer's archive-entry limit. No third-party mod assets are included in the repository.

## Compiled format

The manifest uses UTF-8 text with tab-separated fields. Resource identifiers and selectors cannot contain control characters. Dimension records are optional. All textures precede state definitions. Texture indices start at zero and increase by one.

```text
MINEWAYS_MOD_PACK<TAB>1
dimension<TAB>namespace:dimension<TAB>min_y<TAB>height<TAB>coordinate_scale
texture<TAB>index<TAB>namespace:texture_path<TAB>textures/0000.png
state<TAB>namespace:block[property=value]<TAB>RRGGBB<TAB>alpha_byte
model<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1
element<TAB>from_x<TAB>from_y<TAB>from_z<TAB>to_x<TAB>to_y<TAB>to_z<TAB>axis<TAB>angle<TAB>origin_x<TAB>origin_y<TAB>origin_z<TAB>rescale_0_or_1
face<TAB>direction<TAB>u0<TAB>v0<TAB>u1<TAB>v1<TAB>rotation<TAB>texture_index<TAB>cullface
endstate
```

A new `model` or `element` record closes the previous record of that kind. `endstate` closes the state definition. Axis values are `none`, `x`, `y`, or `z`. Face directions are `down`, `up`, `north`, `south`, `west`, or `east`. A cullface uses one of these directions or `-`. Properties appear in alphabetical order. Omit brackets for an empty selector.
