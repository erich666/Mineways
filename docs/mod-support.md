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

The importer resolves standard JSON model parents and texture aliases. It supports box elements, including inverted bounds and extended coordinates from -256 to 256 pixels, face UVs, face rotations, element rotations, element rescale, blockstate X/Y rotations, and UV locking.

Blockstate variants select a model. Multipart conditions expand into bounded property combinations. Boolean conditions include both `true` and `false`. Unconditional multipart geometry supplies a fallback only when all expanded states are supported. A failed specific selector cannot silently use a generic model.

The Lootr brushable adapter maps `dusted=0..3` to the supplied model stages. Its report notes that player-specific opened or looted appearance is unavailable.

The additional ATM10 adapters use inspected asset data:

- Refined Storage cable models use their supplied colored core geometry. Neighbor-dependent cable extensions are not reconstructed. This also supports Cable Tiers devices that include a cable core in a composite model.
- Bibliocraft tables select the supplied child for the palette's `type` value (`none`, `one`, `straight`, `curve`, `three`, or `all`). Bookcases use their supplied base geometry. Inventory books and seasonal book variants require block-entity data and remain omitted.
- Xycraft connected-texture models use their isolated texture and optional base layer, preserving the declared face mask. Neighbor connections, dynamic tint, and light emission remain approximations.

Missing referenced models or textures still reject the affected definition or selector. These adapters do not substitute invented textures for absent compatibility assets.

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
- Weighted alternatives use the first listed model. Animated textures use the first rendered frame from their metadata. A singleton animation sequence uses the image origin, matching Minecraft 1.21.1. Static rectangular textures retain the entire image and are resampled to square export slots with normalized UVs preserved. The report lists these approximations.
- Arbitrary Java renderers, unsupported custom model loaders and UV coordinates outside 0–16 remain unsupported. The importer supports standard Forge/NeoForge composite children and Fusion base geometry. It reports approximations for connected textures and custom tints. It does not execute mod code.
- PNG import supports non-interlaced and Adam7 images, 8-bit grayscale, RGB, indexed, grayscale-alpha, and RGBA, 16-bit non-indexed formats, and 1-, 2-, and 4-bit grayscale and indexed images. The output always uses RGBA8. Sixteen-bit transparency keys are compared before reducing samples to eight bits.
- A pack catalog can contain more textures than the export atlas. A textured export uses only the textures needed by its selected blocks. This implementation permits 4,096 selected mod textures and 256 MiB of decoded mod texture pixels per export. Larger selections fail explicitly. Exports without textures and Sponge exports bypass these texture limits. Imported textures are resampled to the terrain tile resolution. Only albedo textures are imported.
- Catalog definitions acquire internal block IDs only when a world or schematic uses them. One loaded-pack session can retain 64,975 distinct mod block names, using IDs 561 through 65,535. Each name supports 4,096 observed states. Reloading or clearing the pack resets these assignments. Exhausting this active ID space fails explicitly. It does not reject the larger catalog during pack loading.

The importer indexes archive entries and reads selected assets when needed. Compiled packs share model geometry across state selectors. The runtime validates texture headers when it loads the catalog and decodes only selected export textures. It releases decoded pixels after each export.

The importer limits input files, decompressed asset bytes, PNG pixels, model parent depth, multipart combinations, and compiled elements. It rejects unsafe archive paths, duplicate relevant asset entries, archive symlinks, and source symlinks. The runtime separately validates the compiled manifest and texture paths.

## Active storage and export capacity

The wider block IDs and expanded export texture capacity are implemented. Native capacity tests passed on Windows x64 Debug/Release and macOS ARM64/Intel. Full ATM10 catalog tests also passed on Windows x64 Debug/Release and local macOS ARM64.

| Resource | Implemented limit | Storage |
| --- | --- | --- |
| Active mod block names | 64,975 per loaded-pack session | IDs 561–65,535 in a 16-bit block grid |
| Observed states per block name | 4,096 | Independent 16-bit field with 12 state bits |
| Selected mod textures | 4,096 per textured export | Export slots appended after the built-in terrain slots |
| Decoded mod texture pixels | 256 MiB per export | Only selected textures are decoded |

Chunk readers, schematic readers, caches, map rendering, and exporters keep the full block ID separately from its state. IDs with a zero low byte remain real blocks. State flags for heads and flower pots retain their meanings. Instance hashes use unsigned storage, and material and UV indices can represent the expanded output.

The grid, state, and lighting buffers use 4.5 bytes per voxel: two bytes each for type and state, plus half a byte for light. A full 384-level chunk needs 432 KiB for those buffers, excluding chunk metadata. The grid change adds 96 KiB per full chunk. `Cache_BytesPerChunk` includes the new width, so the selected map-memory budget permits fewer cached chunks instead of increasing that budget.

Imported textures have separate metadata and output slots. They do not replace vacant vanilla cells or alter the built-in terrain atlas. Mosaic dimensions and texture bookkeeping grow for the selected output. Composite swatches follow the imported slots. Solid-color exports retain the vanilla swatches and append compact swatches only for selected mod block types. A high block ID does not require that many color swatches.

The regression suites cover IDs 4,095/4,096, 32,767/32,768, and 65,535, including zero-low-byte boundaries and exact schematic round trips. They also exercise USD instances, expanded material/UV indices, individual textures, mosaics, repeat exports, and rejection of a 4,097-texture selection. These checks passed in [Windows Debug/Release CI](https://github.com/wbreiler/Mineways/actions/runs/43306755350), [macOS Intel CI](https://github.com/wbreiler/Mineways/actions/runs/43306776367), and local macOS ARM64 sanitizer runs. Windows also verifies that headless warning summaries finish without opening a dialog and that individual-texture USD exports do not trigger the mosaic warning.

## Test without a Minecraft installation

```sh
python3 Mac/tests/mod_assets.py
python3 Mac/tests/mod_assets.py --fixture /tmp/mineways-mod-fixture
```

The fixture creates original test assets and `/tmp/mineways-mod-fixture/pack/manifest.mwpack`. It includes a cube with six distinct texture gradients, a rotated non-cube, and a transparent flat panel. These assets can test UV orientation, model transforms, transparency, and schematic state preservation without third-party resources.

## Native build checks

The macOS `make -C Mac test-mods` target checks the importer, registry, and real export paths. `make -C Mac test-mod-limits` checks the complete 16-bit ID range and the 4,096-texture export boundary with original synthetic assets. The Windows CI workflow uses MSBuild for Debug and Release builds. Its mod test runner exercises the resulting native binaries. CI runs those checks on their respective operating systems. A local macOS run does not establish a Windows test result.

## Test the pinned ATM10 catalog on Windows

`tools/fixtures/atm10-1.21.1.json` pins 495 mod JARs and the official Minecraft 1.21.1 client. It records public URLs, file sizes, SHA-1 hashes, and CurseForge project/file IDs. It contains no third-party assets or local installation paths. The November 12, 2026 snapshot requires approximately 1.5 GB of downloads.

The separate downloader verifies every file before creating the importer's source list. It preserves source order: Minecraft client first, then mod JARs sorted by filename. Downloads use HTTPS, bounded retries, streaming size checks, and temporary files. A failed download does not replace a previous file. Existing files are reused only after their size and hash match.

```powershell
python tools/download_mod_assets.py tools/fixtures/atm10-1.21.1.json C:\temp\atm10-assets
python tools/import_mod_assets.py --sources-file C:\temp\atm10-assets\sources.txt --output C:\temp\atm10-pack
python tools/test_mod_windows.py Win\x64\Release\Mineways.exe --pack C:\temp\atm10-pack\manifest.mwpack
python tools/test_mod_windows.py Win\x64\Debug\Mineways.exe --pack C:\temp\atm10-pack\manifest.mwpack
```

Build the corresponding native binaries first. The Debug executable also requires the MSVC AddressSanitizer DLL directory on `PATH`.

In GitHub Actions, select **Windows Build > Run workflow**, choose the branch, and enable `full_atm`. Both Debug and Release jobs download the pinned assets, compile the complete catalog, and run the native tests. The full-pack command includes the synthetic and capacity tests, so the workflow runs them only once per job. Normal push and pull-request jobs do not download ATM10.

Full-pack tests activate every catalog block name through a synthetic schematic palette, then sample actual states for exports. They verify texture pixels, repeat exports, and Sponge geometry/state round trips. They do not test every selector. Full-catalog and capacity invocations have ten-minute timeouts. Smaller fixture invocations have two-minute timeouts. Each workflow job has a one-hour limit. The workflow uploads no downloaded JARs, compiled texture assets, or exports.

The [full-pack Windows Debug/Release run](https://github.com/wbreiler/Mineways/actions/runs/43306762306) passed on November 12, 2026. Each job verified all 496 pinned assets and compiled 42,629 block definitions, 538,059 selectors, and 30,228 textures. Both native builds activated all 42,629 block names and passed 114 sampled states, 131 texture pixel checks, repeat OBJ exports, and Sponge geometry/state round trips. The Release export and round trip took 8.109 and 7.719 seconds. Debug with AddressSanitizer took 454.094 and 445.218 seconds, within the unchanged ten-minute limits. Reusing manifest token storage and avoiding repeated validation of immutable shared geometry resolved the earlier Debug timeout.

## Test an installed mod pack

Compile the assets into a pack first. The optional native smoke test selects actual model states and creates temporary exports. It does not open Minecraft or modify a saved world.

```sh
make -C Mac test-real-mods \
  MOD_PACK=/path/to/compiled-pack/manifest.mwpack
```

This target compiles the shared native core with AddressSanitizer and UndefinedBehaviorSanitizer. It activates every imported block name before the render sample. It checks finite OBJ coordinates and valid face indices, exported texture pixels, repeat exports, and exact Sponge state and geometry round trips. It prints the temporary artifact directory. `selected-states.txt` lists the tested states. Model sampling is deterministic and does not validate every selector or provide a visual comparison with Minecraft.

The capacity-migration baseline macOS tests used the installed ATM10 8.3 instance and its Minecraft 1.21.1 client on November 12, 2026. The four-mod result precedes the wider-ID migration. The complete catalog was retested after that migration:

| Imported subset | Loaded blocks / selectors / textures | Tested states | Exported faces | Pixel-checked textures |
| --- | --- | --- | --- | --- |
| AllTheModium 3.2.1, Applied Energistics 2 19.2.18, Iron Furnaces 4.3.2, Simply Light 1.5.3 | 379 / 4343 / 469 | 146 | 2282 | 96 |
| All 495 installed ATM10 mod JARs, plus the matching client assets | 42629 / 538059 / 30228 | 139 | 3860 | 136 |

The latest four-mod run used AddressSanitizer and UndefinedBehaviorSanitizer. It explicitly tested 16 supported drive orientations, both quartz-glass blocks, the spatial pylon, invisible matrix-frame identity, and four levels of each fluid. Its importer report lists 5 unsupported block definitions, 103 unsupported selectors, and 84 warnings. The initial importer rejected 44 block definitions in this subset. The 103 unsupported selectors include 88 AE2 orientations that were previously accepted with an incorrect rotation. The other 15 were already unsupported. Earlier native tests also covered all four dusting stages of both Lootr brushable blocks. The pack includes three dimension metadata records. These checks tested block exports and did not navigate those dimensions.

A separate actual-block regression verifies `justdirethings:goopatternblock[goostage=0]`. Its transparent top/map texture must not discard its visible side geometry. The native test checks its exported faces, texture pixels, and schematic round trip.

That baseline compiled catalog contains 84,940 shared geometries, 475,456 elements, and 25 dimension records. Its report lists 9,522 unsupported block definitions, 9,467 unsupported selectors, and 13,230 warnings. Every installed mod JAR was indexed. Across the complete catalog, 192 AE2 orientations previously accepted with an incorrect Z rotation are now explicitly unsupported. Unsupported records remain explicit in the report.

The baseline full-catalog test passed with AddressSanitizer and UndefinedBehaviorSanitizer after the wider-ID migration. It activated all 42,629 block names before export, reaching internal ID 43,189. The 139 tested states included 37 explicit adapter and transparency regressions. All 136 imported texture pixel checks passed, as did repeated OBJ exports and exact Sponge state/geometry round trips. Catalog loading took 8.873 seconds with zero decoded texture pixels. The complete test took 15.002 seconds and exited successfully on the local macOS host. These ARM64 measurements are separate from the Windows results above. Neither suite tests every model selector.

Catalog counts describe asset definitions, not the game's registered block types. Optional compatibility resources can refer to absent mods or models. The importer reports those missing resources explicitly. No third-party mod assets are included in the repository.

## Additional ATM10 model coverage

The next compatibility pass recovered 1,547 previously unsupported block definitions from the same 495-mod snapshot. The changes cover inverted and oversized JSON elements, rectangular textures, Adam7 and 16-bit PNGs, and the Refined Storage, Bibliocraft, and Xycraft loaders described above. All 325 inspected PNGs in the newly supported formats match Mineways' native decoder byte-for-byte after RGBA8 conversion.

Real-pack testing also found a Debug-only assertion that limited one transformation to 400 vertices. A recovered Bibliocraft lamp has 464 vertices. The exporter now checks the actual allocated vertex range. Synthetic native tests verify all 512 transformed positions of a rotated 64-element model, plus 384 faces and Sponge identity. Windows tests include the same complex model, inverted winding, and extended bounds.

The final catalog has 44,176 supported block definitions, 561,393 selectors, 31,167 textures, 87,768 shared geometries, 503,190 elements, and 25 dimensions. Unsupported definitions fell from 9,522 to 7,975. Unsupported state report entries fell from 9,467 to 2,894. All 538,059 previously supported selectors remain supported, with 23,334 additional selectors. The report contains 13,806 warnings.

Local macOS ARM64 tests passed with AddressSanitizer and UndefinedBehaviorSanitizer. The full catalog activated every supported block name through ID 44,736. The export sample covered 214 states and 154 texture pixel checks, including all Acacia Bibliocraft table states and the Silent Gear animation regression. Repeated OBJ exports and exact Sponge state/geometry round trips passed. Catalog loading took 9.185 seconds. All 54 Python importer tests and the native capacity, security, and app-build checks passed. The [Windows Debug/Release native regression suite](https://github.com/wbreiler/Mineways/actions/runs/43314953884) and [macOS Intel CI](https://github.com/wbreiler/Mineways/actions/runs/43314987701) also passed.

The [full-pack Windows Debug/Release run](https://github.com/wbreiler/Mineways/actions/runs/43314983996) passed with the same catalog counts. Each job verified all 496 pinned input assets, activated all 44,176 supported block names, and passed 135 sampled states, 144 texture pixel checks, repeat OBJ exports, and exact Sponge geometry/state round trips. Release completed the export and round trip in 9.594 and 9.172 seconds. Debug with AddressSanitizer completed them in 478.031 and 468.421 seconds, within the unchanged ten-minute limits.

The largest remaining definition failures are missing textures (4,699), models without standard geometry (1,754), unsupported custom loaders (821), and missing models (560). Many missing resources belong to optional compatibility content. Remaining custom loaders and block-entity renderers require separate adapters rather than substitute cubes. Out-of-range UVs remain unsupported.

The tests sample exports rather than compare every selector visually with Minecraft. Print dimensions and material estimates use occupied voxel bounds, so large model overhangs can exceed those estimates. Scaling by block size preserves the supplied geometry.

## Static custom renderer coverage

The importer now reads NeoForge OBJ triangles and quads from supplied OBJ/MTL assets. It preserves polygon winding, per-corner UVs, blockstate rotations, inherited component visibility, and supported TRSR root transforms. Material diffuse colors are baked into bounded derived textures. Source vertex normals become flat face normals. Animation, ambient shading, biome tint, and neighbor culling remain explicit approximations. Missing texture resources still fail. Matrix and four-component quaternion transforms remain unsupported.

Modern Industrialization machines use their asset-defined casings and overlays. The static default is north-facing and inactive. Actual block-entity orientation, active appearance, casing upgrades, and output/extraction configuration are not available. The `use_block_model` adapter accepts one unrotated generic model and rejects ambiguous defaults.

Refined Storage and Cable Tiers disk interfaces retain direction and active-state selectors. Disk drives and ExtendedAE drives use their available base geometry. Inserted disks, cell chassis, and contents indicators require block-entity data and remain omitted. Reports name these limitations. Supplementaries slanted awnings apply the loader's extra rotation to the original elements and UVs.

Polygon exports also required removing the legacy 456-entry normal-table limit. The exporter grows that table up to its signed-short index limit and reports capacity or allocation failures. Native regression tests exercise more than 456 distinct normals and repeated exports.

The same full ATM10 snapshot now compiles 44,483 block definitions, 564,698 selectors, 31,388 textures, 88,162 shared geometries, 504,040 elements, 13,882 polygons, and 25 dimensions. This adds 307 block definitions and 3,305 selectors without losing any previously supported selector. All 136 slanted-awning selectors are recovered. The report lists 7,668 unsupported definitions, 2,727 unsupported state records, and 14,255 warnings. Remaining definition failures are led by missing textures (4,813), absent standard geometry (1,754), missing models (560), and custom loaders (400).

The ARM64 macOS ASan/UBSan run activated all 44,483 supported names and checked 153 selected states, 9,316 faces, and 208 exported textures against source pixels. Repeated OBJ exports and exact Sponge geometry/state round trips passed. Synthetic tests cover triangle/quad winding and UVs, rotated meshes, mosaic/color exports, single-block USD, malformed v3 records, float precision failure, and 800 distinct normals. Testing also fixed an existing single-instance USD chunk iteration overrun.

## Compiled format

The importer writes version 3 when a catalog contains polygons and version 2 otherwise. The runtime reads versions 1, 2, and 3. All versions use UTF-8 text with tab-separated fields. Resource identifiers and selectors cannot contain control characters. Dimension records are optional. Texture and geometry indices each start at zero and increase by one. References must use earlier records.

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

A `geometry` record stores reusable elements and faces. Version 3 also permits `polygon<TAB>texture_index<TAB>vertex_count`, followed by five tab-separated numbers per vertex: `x y z u v`. A polygon has three or four vertices. Positions use pixels within -256..256, and UVs use 0..16 with V downward. Degenerate polygons are invalid. Polygon limits are 16,384 per geometry and 1,000,000 per catalog. Each `usemodel` references that geometry with its own blockstate rotations and UV-lock setting. A state can reference several models. `endgeometry` and `endstate` close their respective definitions. A new `element` record closes the previous element.

Version 1 uses `model<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1` inside each state, followed by inline elements and faces. It does not use shared geometry records.

Version 2 also permits an explicit `invisible` line between `state` and `endstate`. Such a state must have alpha zero and no models. It keeps a known block's schematic identity without rendering it. Unmarked empty states, mixed invisible/model states, and duplicate markers are invalid.

The state color and alpha describe the map sample. Exported model opacity comes from its face textures.

Element coordinates and rotation origins must be within -256..256 pixels. Inverted bounds preserve the original face winding and UV mapping.

Axis values are `none`, `x`, `y`, or `z`. Face directions are `down`, `up`, `north`, `south`, `west`, or `east`. A cullface uses one of these directions or `-`. Properties appear in alphabetical order. Omit brackets for an empty selector.

Catalog limits are 100,000 block definitions, 1,000,000 selectors, 250,000 shared geometries, 4,000,000 elements, 131,072 textures, and a 512 MiB manifest. These are catalog limits. The active block IDs and selected export textures have the separate limits described above.
