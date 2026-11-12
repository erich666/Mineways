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

For a large modpack, use `--sources-file sources.txt` to avoid the Windows command-line length limit. The file contains one unquoted path per line in UTF-8, with or without a BOM. Blank lines are ignored. The file must be at most 1 MiB. Relative paths use the current working directory. Listed sources precede any positional sources, so a final positional resource pack can override them.

This PowerShell example lists the Minecraft client first, then all installed mod JARs in filename order. Set the two input paths for your launcher:

```powershell
$clientJar = 'C:\path\to\minecraft-1.21.1-client.jar'
$modsFolder = 'C:\path\to\instance\minecraft\mods'
$sourcePaths = @($clientJar) + @(Get-ChildItem -LiteralPath $modsFolder -File -Filter '*.jar' | Sort-Object Name | ForEach-Object { $_.FullName })
[IO.File]::WriteAllLines((Join-Path $PWD 'sources.txt'), $sourcePaths, [Text.UTF8Encoding]::new($false))
python tools/import_mod_assets.py --sources-file sources.txt --output C:\path\to\new-mineways-pack
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

Blockstate variants select a model. Multipart conditions expand into bounded property combinations. Boolean conditions include both `true` and `false`. Unconditional multipart geometry supplies a fallback only when all expanded states are supported. A failed specific selector cannot silently use a generic model.

The Lootr brushable adapter maps `dusted=0..3` to the supplied model stages. Its report notes that player-specific opened or looted appearance is unavailable.

Narrow adapters for the inspected AE2 19.2.18 assets use the empty drive model, isolated quartz-glass appearance, and unlinked, unpowered spatial-pylon appearance. They do not reconstruct installed storage cells, activity indicators, or connections from block-entity data. `ae2:matrix_frame` is explicitly invisible, as in the mod. It produces no render geometry but retains its state through Sponge export and import. An empty model from another block does not automatically become invisible.

The four Allthemodium 3.2.1 fluids use their actual still and flowing textures and the `level` property. Their flat surfaces approximate the fluid renderer. Neighbor slopes, vertical continuity, hidden internal faces, and emission are not reproduced. Reports identify these approximations. AE2's custom Z rotations are mapped to equivalent supported X/Y rotations when possible. Other orientations are reported as unsupported rather than silently using an incorrect orientation.

Five definitions remain unsupported in the original four-mod subset:

| Definition | Missing requirement |
| --- | --- |
| `ae2:paint` | The block entity's bounded `dots` payload supplies each decal's face, position, AEColor, and luminous flag. A palette state cannot identify those decals. |
| `ae2:cable_bus` | The central cable and six directional part compounds identify cables, attachments, and facades. Connections and channel activity can also depend on neighboring blocks and runtime network state. |
| `iceandfire:allthemodium_pile`, `iceandfire:unobtainium_pile`, `iceandfire:vibranium_pile` | Allthemodium ships blockstate references but no corresponding pile models. No installed JAR or local resource pack supplies them. The inspected Ice and Fire registry registers gold, silver, and copper piles, with no registration found for these three compatibility names. They appear to be stale compatibility resources. |

Paint and cable support need a new modern block-entity data path. `nbtGetBlocks` currently retains only legacy `TileEntities` information, and `BlockEntity` holds four bytes. A bounded, position-keyed payload must survive chunk caching, reach the exporter, and round-trip through Sponge before those adapters can preserve actual block contents. Paint is the smaller first adapter. Cable rendering additionally needs versioned part parsers, facade models, and neighbor connections. Empty cable or paint proxy cubes would misrepresent the saved world.

The current implementation has these explicit limits:

- A state with no matching model selector uses the configured unknown-block fallback (bedrock by default in the application). Chunk loading reports its namespaced state and continues. This includes modern palettes that omit default properties when a mod provides only explicit property selectors: JSON assets do not reveal Java-defined defaults, so Mineways does not guess them. Sponge imports also use air for these states. Unsupported states cannot retain their identity when exported.
- Weighted alternatives use the first listed model. Animated textures use the first square frame. The report lists these approximations.
- Arbitrary Java renderers, unsupported custom model loaders, inverted elements, and UV coordinates outside 0–16 remain unsupported. The importer supports standard Forge/NeoForge composite children and Fusion base geometry. It reports approximations for connected textures and custom tints. It does not execute mod code.
- PNG import supports non-interlaced 8-bit grayscale, RGB, indexed, grayscale-alpha, and RGBA images, plus 1-, 2-, and 4-bit grayscale and indexed images. The output always uses RGBA.
- A pack catalog can contain more textures than the export atlas. A textured export uses only the textures needed by its selected blocks. This release permits 915 selected mod textures and 256 MiB of decoded mod texture pixels per export. Larger selections fail explicitly. Exports without textures and Sponge exports bypass these texture limits. Imported textures are resampled to the terrain tile resolution. Only albedo textures are imported.
- Catalog definitions acquire internal block IDs only when a world or schematic uses them. One loaded-pack session can retain 3,522 distinct mod block names and 4,096 observed states per name. Reloading or clearing the pack resets these assignments. Exhausting this active ID space fails explicitly. It does not reject the larger catalog during pack loading.

The importer indexes archive entries and reads selected assets when needed. Compiled packs share model geometry across state selectors. The runtime validates texture headers when it loads the catalog and decodes only selected export textures. It releases decoded pixels after each export.

The importer limits input files, decompressed asset bytes, PNG pixels, model parent depth, multipart combinations, and compiled elements. It rejects unsafe archive paths, duplicate relevant asset entries, archive symlinks, and source symlinks. The runtime separately validates the compiled manifest and texture paths.

## Raising the active limits

The catalog already holds the full installed ATM10 asset set. The remaining limits apply to observed blocks and selected export textures. Raising their constants alone would corrupt packed block IDs or overrun fixed texture tables. This pass assesses these migrations and retains the current limits.

| Limit | Proposed next target | Required change |
| --- | --- | --- |
| 3,522 active mod block names | 64,975 mod names within 65,536 total type slots | Separate a 16-bit block type from the 16-bit state field throughout the chunk and schematic readers, cache, map, and exporters. |
| 915 selected mod textures | Initially 4,096 selected mod textures | Allocate imported texture slots per export, independently of vacant vanilla terrain cells. Grow UV and material bookkeeping with those slots. |

The block migration reaches `cache`, `nbt`, `region`, `MinewaysMap`, both platform shells, schematic buffers, instance hashes, and type-indexed tables. Instance hashes currently shift a signed type by 16 bits, so IDs above 32,767 also need unsigned hash storage and decoding. It must remove the type-extension nibble, byte casts, low-byte air checks, and hard-coded 12-bit type masks together. `BoxCell` already uses 16-bit types. The chunk grid does not. A 16-bit grid adds one byte per voxel: 96 KiB per full-height 384-block chunk, or about 562.5 MiB for 6,000 such cached chunks. Update the existing `Cache_BytesPerChunk` calculation with the new grid width so the map-memory budget reduces the cache entry count and holds the selected RAM budget constant. State IDs can retain the present 4,096-per-block limit.

The texture migration reaches `prepareModTextures`, swatch lookup, `Model::tileList`, UV lists, individual PNG writers, mosaics, PBR channels, and OBJ/USD/VRML material writers. `TOTAL_TILES` currently marks both the end of the vanilla table and the start of composite swatches. `NUM_MAX_SWATCHES` is also fixed at 4,096. Imported slots need their own metadata and a non-overlapping allocation range. Start with individual-image OBJ/USD exports, then add dynamic mosaic sizing and the remaining formats. Preserve the decoded-pixel budget and explicit allocation failures. Do not enlarge every terrain image or duplicate the full catalog's textures in memory.

Required migration checks include active IDs at 4,095/4,096, 32,767/32,768, and 65,535, including low-byte-zero boundaries, exact schematic round trips, vanilla geometry parity, and one export with over 915 distinct selected textures. The existing large-catalog test selects only three textures and cannot establish that last property. Test individual images, mosaics, UV seams, PBR neutrality, repeat exports, and allocation failure on Windows Debug/Release and macOS sanitizers.

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
| AllTheModium 3.2.1, Applied Energistics 2 19.2.18, Iron Furnaces 4.3.2, Simply Light 1.5.3 | 379 / 4343 / 469 | 146 | 2282 | 96 |
| All 495 installed ATM10 mod JARs, plus the matching client assets | 42629 / 538059 / 30228 | 139 | 3860 | 136 |

The latest four-mod run used AddressSanitizer and UndefinedBehaviorSanitizer. It explicitly tested 16 supported drive orientations, both quartz-glass blocks, the spatial pylon, invisible matrix-frame identity, and four levels of each fluid. Its importer report lists 5 unsupported block definitions, 103 unsupported selectors, and 84 warnings. The initial importer rejected 44 block definitions in this subset. The 103 unsupported selectors include 88 AE2 orientations that were previously accepted with an incorrect rotation. The other 15 were already unsupported. Earlier native tests also covered all four dusting stages of both Lootr brushable blocks. The pack includes three dimension metadata records. These checks tested block exports and did not navigate those dimensions.

A separate actual-block regression verifies `justdirethings:goopatternblock[goostage=0]`. Its transparent top/map texture must not discard its visible side geometry. The native test checks its exported faces, texture pixels, and schematic round trip.

The complete compiled catalog contains 84,940 shared geometries, 475,456 elements, and 25 dimension records. Its report lists 9,522 unsupported block definitions, 9,467 unsupported selectors, and 13,230 warnings. Every installed mod JAR was indexed. Across the complete catalog, 192 AE2 orientations previously accepted with an incorrect Z rotation are now explicitly unsupported. Unsupported records remain explicit in the report.

The full native sanitizer smoke test loaded the catalog in 13.828 seconds with zero decoded texture pixels. It included 37 explicit adapter and transparency regression states alongside the deterministic sample. The complete export test took 19.97 seconds and reached 1.64 GiB peak resident memory. That peak includes sanitizer and test-harness overhead. No mod block IDs were allocated before selecting states. These measurements use the local macOS host.

Catalog counts describe asset definitions, not the game's registered block types. Optional compatibility resources can refer to absent mods or models. The importer reports those missing resources explicitly. No third-party mod assets are included in the repository.

## Compiled format

The importer writes version 2. The runtime also reads existing version 1 packs. Both formats use UTF-8 text with tab-separated fields. Resource identifiers and selectors cannot contain control characters. Dimension records are optional. Texture and geometry indices each start at zero and increase by one. References must use earlier records.

```text
MINEWAYS_MOD_PACK<TAB>2
dimension<TAB>namespace:dimension<TAB>min_y<TAB>height<TAB>coordinate_scale
texture<TAB>index<TAB>namespace:texture_path<TAB>textures/0000.png
geometry<TAB>geometry_index
element<TAB>from_x<TAB>from_y<TAB>from_z<TAB>to_x<TAB>to_y<TAB>to_z<TAB>axis<TAB>angle<TAB>origin_x<TAB>origin_y<TAB>origin_z<TAB>rescale_0_or_1
face<TAB>direction<TAB>u0<TAB>v0<TAB>u1<TAB>v1<TAB>rotation<TAB>texture_index<TAB>cullface
endgeometry
state<TAB>namespace:block[property=value]<TAB>RRGGBB<TAB>alpha_byte
usemodel<TAB>geometry_index<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1
endstate
```

A `geometry` record stores reusable elements and faces. Each `usemodel` references that geometry with its own blockstate rotations and UV-lock setting. A state can reference several models. `endgeometry` and `endstate` close their respective definitions. A new `element` record closes the previous element.

Version 1 uses `model<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1` inside each state, followed by inline elements and faces. It does not use shared geometry records.

Version 2 also permits an explicit `invisible` line between `state` and `endstate`. Such a state must have alpha zero and no models. It keeps a known block's schematic identity without rendering it. Unmarked empty states, mixed invisible/model states, and duplicate markers are invalid.

The state color and alpha describe the map sample. Exported model opacity comes from its face textures.

Axis values are `none`, `x`, `y`, or `z`. Face directions are `down`, `up`, `north`, `south`, `west`, or `east`. A cullface uses one of these directions or `-`. Properties appear in alphabetical order. Omit brackets for an empty selector.

Catalog limits are 100,000 block definitions, 1,000,000 selectors, 250,000 shared geometries, 4,000,000 elements, 131,072 textures, and a 512 MiB manifest. These are catalog limits. The active block IDs and selected export textures have the separate limits described above.
