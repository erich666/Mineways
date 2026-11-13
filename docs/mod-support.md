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

This NeoForge example lists the Minecraft client, the matching NeoForge universal JAR, then installed mods in filename order. Set the three input paths for your launcher:

```powershell
$clientJar = 'C:\path\to\minecraft-1.21.1-client.jar'
$loaderJar = 'C:\path\to\neoforge-21.1.259-universal.jar'
$modsFolder = 'C:\path\to\instance\minecraft\mods'
$sourcePaths = @($clientJar, $loaderJar) + @(Get-ChildItem -LiteralPath $modsFolder -File -Filter '*.jar' | Sort-Object Name | ForEach-Object { $_.FullName })
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

The four Allthemodium 3.2.1 fluids use their actual still and flowing textures and the `level` property. Their flat surfaces approximate the fluid renderer. Neighbor slopes, vertical continuity, hidden internal faces, and emission are not reproduced. Reports identify these approximations. AE2 custom Z rotations use equivalent X/Y rotations when possible. Other orientations use an explicit Z quarter-turn before X and Y. UV locking uses the same rotation order.

XyCraft Core 0.7.53 CloudFX sprites use the shipped noise algorithm with float32 arithmetic and a fixed seed of zero. The importer generates the first uploaded frame after 128 warm-up steps. This is a reproducible static appearance, not the random animation phase of a running game. The three supported sprites require their supplied atlas declarations. Explicit PNG assets take precedence. Standard JSON faces also preserve NeoForge static ARGB colors by baking them into derived textures. Face metadata replaces element metadata, as in NeoForge. Block/sky lighting and ambient-occlusion overrides are reported but are not exported.

Five definitions remain unsupported in the original four-mod subset:

| Definition | Missing requirement |
| --- | --- |
| `ae2:paint` | The block entity's bounded `dots` payload supplies each decal's face, position, AEColor, and luminous flag. A palette state cannot identify those decals. |
| `ae2:cable_bus` | The central cable and six directional part compounds identify cables, attachments, and facades. Connections and channel activity can also depend on neighboring blocks and runtime network state. |
| `iceandfire:allthemodium_pile`, `iceandfire:unobtainium_pile`, `iceandfire:vibranium_pile` | Allthemodium ships blockstate references but no corresponding pile models. No installed JAR or local resource pack supplies them. The inspected Ice and Fire registry registers gold, silver, and copper piles, with no registration found for these three compatibility names. They appear to be stale compatibility resources. |

Modern chunk and Sponge readers now retain bounded IE model-offset context, as described below. This path does not retain AE2 paint dots, cable parts, inventories, or arbitrary block-entity payloads. Paint and cable support still require their own validated payload schemas and render adapters. Cable rendering also needs facade models and neighbor connections. Empty cable or paint proxy cubes would misrepresent the saved world.

The current implementation has these explicit limits:

- A state with no matching model selector uses the configured unknown-block fallback (bedrock by default in the application). Chunk loading reports its namespaced state and continues. This includes modern palettes that omit default properties when a mod provides only explicit property selectors: JSON assets do not reveal Java-defined defaults, so Mineways does not guess them. Sponge imports also use air for these states. Unsupported states cannot retain their identity when exported.
- Weighted alternatives use the first listed model. Animated textures use the first rendered frame from their metadata. A singleton animation sequence uses the image origin, matching Minecraft 1.21.1. Static rectangular textures retain the entire image and are resampled to square export slots with normalized UVs preserved. The report lists these approximations.
- Arbitrary Java renderers and unsupported custom model loaders remain unsupported. Out-of-range UVs use the bounded texture repetition described below. The importer supports standard Forge/NeoForge composite children and Fusion base geometry. It reports approximations for connected textures and custom tints. It does not execute mod code.
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

`tools/fixtures/atm10-1.21.1.json` pins 495 mod JARs, the official Minecraft 1.21.1 client, and the matching NeoForge 21.1.259 universal JAR. It records public URLs, file sizes, SHA-1 hashes, and CurseForge project/file IDs. It contains no third-party assets or local installation paths. The November 12, 2026 snapshot requires approximately 1.5 GB of downloads.

The separate downloader verifies every file before creating the importer's source list. It preserves source order: Minecraft client first, then NeoForge runtime assets, then mod JARs sorted by filename. Downloads use HTTPS, bounded retries, streaming size checks, and temporary files. A failed download does not replace a previous file. Existing files are reused only after their size and hash match.

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

The largest remaining definition failures are missing textures (4,699), models without standard geometry (1,754), unsupported custom loaders (821), and missing models (560). Many missing resources belong to optional compatibility content. Remaining custom loaders and block-entity renderers require separate adapters rather than substitute cubes. Out-of-range UVs remained unsupported in that pass.

The tests sample exports rather than compare every selector visually with Minecraft. Print dimensions and material estimates use occupied voxel bounds, so large model overhangs can exceed those estimates. Scaling by block size preserves the supplied geometry.

## Static custom renderer coverage

The importer now reads NeoForge OBJ triangles and quads from supplied OBJ/MTL assets. It preserves polygon winding, per-corner UVs, blockstate rotations, inherited component visibility, and supported TRSR root transforms. Material diffuse colors are baked into bounded derived textures. Source vertex normals become flat face normals. Animation, ambient shading, biome tint, and neighbor culling remain explicit approximations. Missing texture resources still fail. Matrix and four-component quaternion transforms remain unsupported.

Modern Industrialization machines use their asset-defined casings and overlays. The static default is north-facing and inactive. Actual block-entity orientation, active appearance, casing upgrades, and output/extraction configuration are not available. The `use_block_model` adapter accepts one unrotated generic model and rejects ambiguous defaults.

Refined Storage and Cable Tiers disk interfaces retain direction and active-state selectors. Disk drives and ExtendedAE drives use their available base geometry. Inserted disks, cell chassis, and contents indicators require block-entity data and remain omitted. Reports name these limitations. Supplementaries slanted awnings apply the loader's extra rotation to the original elements and UVs.

Polygon exports also required removing the legacy 456-entry normal-table limit. The exporter grows that table up to its signed-short index limit and reports capacity or allocation failures. Native regression tests exercise more than 456 distinct normals and repeated exports.

The same full ATM10 snapshot now compiles 44,483 block definitions, 564,698 selectors, 31,388 textures, 88,162 shared geometries, 504,040 elements, 13,882 polygons, and 25 dimensions. This adds 307 block definitions and 3,305 selectors without losing any previously supported selector. All 136 slanted-awning selectors are recovered. The report lists 7,668 unsupported definitions, 2,727 unsupported state records, and 14,255 warnings. Remaining definition failures are led by missing textures (4,813), absent standard geometry (1,754), missing models (560), and custom loaders (400).

The ARM64 macOS ASan/UBSan run activated all 44,483 supported names and checked 153 selected states, 9,316 faces, and 208 exported textures against source pixels. Repeated OBJ exports and exact Sponge geometry/state round trips passed. Synthetic tests cover triangle/quad winding and UVs, rotated meshes, mosaic/color exports, single-block USD, malformed v3 records, float precision failure, and 800 distinct normals. Testing also fixed an existing single-instance USD chunk iteration overrun. A second full-pack run exported all 3,305 newly supported selectors together with the baseline sample: 3,432 states, 178,056 faces, and 446 pixel-checked textures. Repeated exports and exact Sponge round trips passed.

The [Windows Debug/Release fixture run](https://github.com/wbreiler/Mineways/actions/runs/43324160854) and [macOS Intel build](https://github.com/wbreiler/Mineways/actions/runs/43324197091) passed at `ade5fd7`. Both platforms passed all 76 importer tests. The [full-pack Windows run](https://github.com/wbreiler/Mineways/actions/runs/43324187486) compiled the same catalog counts. Both Debug and Release passed 165 sampled states, 221 texture pixel checks, repeated OBJ exports, and exact Sponge geometry/state round trips. Release completed the export and round trip in 10.031 and 9.547 seconds. Debug with AddressSanitizer completed them in 496.188 and 484.047 seconds, within the unchanged ten-minute process limits.

## Further model compatibility

AE2 Z rotation support recovers all 200 previously rejected selectors across 16 block types. The full catalog has 564,898 selectors after this change, with no prior selector lost. Native sanitizer tests cover all 24 cube orientations, element-local rotations, polygons, UV locking, neighbor culling, and OBJ/USD/mosaic/Sponge output. A full-pack macOS run exported all 200 recovered selectors alongside the baseline sample: 352 states, 14,818 faces, and 263 pixel-checked textures. Repeated OBJ exports and exact Sponge geometry/state round trips passed.

The UV pass recovers all 45 targeted block definitions and 672 rejected selectors. It also recovers one additional definition whose unused faces referenced a missing texture. The catalog gains 46 definitions and 5,460 selectors without losing prior support. Zero-area faces are removed before texture and UV validation, matching the native emitter. Visible out-of-range UVs repeat their original texture with an explicit report warning. This is a portable approximation: Minecraft extrapolates into neighboring atlas pixels, and that live atlas layout is unavailable from asset files alone.

The exporter splits affected faces at texture boundaries after UV locking. Each patch uses the original texture, so individual and mosaic exports preserve texel density without enlarged replacement images. Element rotations, blockstate rotations, face winding, and billboard rules retain their existing behavior. Untextured exports retain their original topology. The resulting catalog contains 44,529 definitions, 570,358 selectors, 31,480 textures, 88,663 geometries, 508,633 elements, and 13,882 polygons. Its report lists 7,622 unsupported definitions and 1,855 unsupported states. All 5,460 new selectors passed native macOS sanitizer exports across 11 compact synthetic worlds, including repeated OBJ exports, source texture pixel checks, and exact Sponge geometry/state round trips. All 87 Python tests and the focused native UV suite passed.

The CloudFX pass recovers all 423 targeted definitions and 2,059 selectors across XyCraft World, Override, Machines, and Allthecompressed. No previously supported selector is lost. The catalog contains 44,952 definitions, 572,417 selectors, 31,611 textures, 88,936 geometries, 509,160 elements, and 18,866 polygons. Its report lists 7,199 unsupported definitions and 1,855 unsupported states. All 2,059 new selectors passed macOS sanitizer exports in five compact synthetic worlds, with source texture pixel checks, repeated OBJ exports, and exact Sponge geometry/state round trips. All 103 importer tests passed. Synthetic tests compare the generated pixels with an independent Java float reference and verify atlas stacking, PNG overrides, ARGB inheritance, transparency, and budget rollback.

## Immersive Engineering models

The adapters target Immersive Engineering 12.4.2 for Minecraft 1.21.1. The `ie_obj` adapter reads supplied OBJ geometry, textures, render layers, and verified static callback defaults. It follows IE object-group visibility and V-coordinate conversion. IE ignores OBJ material diffuse colors; supported callback colors use derived RGBA textures. Positive forward OBJ indices are supported. Negative indices remain relative to the attributes available at each face.

The `basic_split` adapter divides the source model into cells before applying blockstate rotations. It preserves clipped UVs and moves each part into local cell coordinates. Overhangs join the nearest declared part, using the shipped Java set order for equal distances. Mirrored models preserve winding. Each world cell selects only its corresponding part, so the exporter does not duplicate the complete machine at every cell.

Cell selection uses palette properties where the shipped provider permits this. Posts use `post_slave` and `horizontal_offset`. Workbenches, circuit tables, Tesla coils, and current transformers use `multiblockslave`. Other providers require saved context:

- Cloches, sample drills, blast-furnace preheaters, and transformers use the root `dummy` NBT integer.
- Multiblock dummy entities use the `posInMB` integer array with exactly three coordinates. The adapter applies the verified master offset and mirrored-state mapping.
- Multiblock master entities use their verified `_master` entity ID and select the origin part. Dummy entities must have the corresponding `_dummy` ID.

Modern chunks supply these fields through `block_entities`. Sponge v2 supplies them in `BlockEntities`; Sponge v3 supplies them in each entry's `Data` compound. Entity positions associate the context with the correct voxel. The runtime retains context separately from the canonical namespaced block state. Different cells can share one palette identity while selecting different geometry. Context variants count toward the existing per-block state limit.

A render export reports missing required fields or palette properties. Unknown part coordinates also stop the export. Mineways does not assume the origin part or silently hide the affected cell. Sponge exports can preserve the block identity and recognized context even when rendering cannot proceed. They preserve these rendering fields, not complete inventories or machine configuration.

Entity IDs must match the declared provider and contain at most 255 bytes. Saved `dummy` values use -16..16; `posInMB` coordinates use -16..32. Entity lists permit at most 65,536 records and 16 MiB. Invalid field types, duplicate positions, and positions outside the containing region are rejected. The importer limits each split model to 256 declared parts and 65,536 generated fragments. Unsupported Java set collision-tree cases fail explicitly.

Static callback defaults do not reproduce every saved or live appearance. Reports identify omitted connection arms, pipe connections, chute walls, installed boards, shelf crates, blueprints, crops, machine contents, and activity indicators. Shader skins, animations, emitted lighting, and separate block-entity renderer geometry remain omitted. Structural arms use the verified one-block default slope; saved length, attachment, and direction are not reconstructed. The bounded cell-offset support does not remove these callback limitations.

Stable triangulation avoids unnecessary sliver triangles without changing convex geometry or affine UV interpolation. Faces with nonaffine UVs retain their original triangulation. Export vertex positions now use double precision through interpolation, transformation, scaling, and normal calculation. OBJ and ASCII STL write enough digits to retain those positions. This fixes thin IE fragments that previously collapsed in wide selections. The vertex buffer uses 12 additional bytes per vertex. Global USD, VRML, and binary STL coordinates remain float-limited. A preflight rejects face collapse or reversal before replacing existing model files. Existing USD instancing can preserve these details through local prototype coordinates when its float instance anchors are representable. Unrepresentable anchors also fail before output files open.

The full ATM10 8.3 catalog recovers all 38 targeted `ie_obj` definitions and all 35 `basic_split` definitions. The verified invisible `fake_light` adds one more definition. Together they add 74 definitions and 337 selectors without losing prior support. Five visible blocks still requiring separate renderers remain unsupported: bucket wheels, watermills, windmills, shader banners, and wall shader banners.

The catalog contains 45,026 definitions, 572,754 selectors, 31,680 textures, 90,446 geometries, 509,160 elements, 34,642 polygons, and 25 dimensions. The report lists 7,125 unsupported definitions and 1,855 unsupported states. All 337 new selectors passed native macOS sanitizer exports, source texture pixel checks, repeated OBJ exports, and exact Sponge geometry/state round trips. All 6,224 split-part cases passed the same native checks across 13 compact test worlds. All 126 Python tests passed. Native fixtures also cover saved context, missing-context errors, malformed or truncated NBT, entity limits, and preservation of an existing output file when preflight rejects the export.

The [Windows Debug/Release fixture run](https://github.com/wbreiler/Mineways/actions/runs/43346911561) passed at `cc8a76c`. Both configurations passed all 126 importer tests, native geometry and schematic fixtures, the uint16 block-ID boundary, and imported texture capacity checks. The [macOS Intel build and test run](https://github.com/wbreiler/Mineways/actions/runs/43346915704) also passed, including app packaging and artifact upload.

The [full-pack Windows run](https://github.com/wbreiler/Mineways/actions/runs/43346911672) compiled the same catalog from all 496 pinned assets. Both Debug and Release passed 893 sampled states, 390 texture pixel checks, repeated OBJ exports, and exact Sponge geometry/state round trips. Release export and round-trip processes took 9.594 and 8.875 seconds. Debug with AddressSanitizer took 477.719 and 467.063 seconds, within the unchanged ten-minute process limits.

## Further compatibility and precision fixes

Multipart conditions now normalize JSON integer values to decimal property strings, matching the shipped selector reader. Existing property validation, boolean handling, and expansion limits still apply. Unknown cullface strings become uncullable faces with an explicit warning, matching Minecraft’s direction lookup. Malformed nonstring fields still fail.

Sophisticated Storage `simple_material` models export their supplied default base without the custom-material overlay. Limited barrels export the supplied acacia/default-tier parts, including flat variants, part-level inheritance, and nested composite geometry. The adapters preserve blockstate transforms and resource overrides. Reports identify omitted saved wood choices, dyes, custom materials, packing, locks, and displayed items.

Missing texture/model failures now include `missing_resource` details: resource kind, name, and `indexed_namespace_present`. The report’s `missing_resources` summary counts failed definitions and states under `namespace_indexed` and `namespace_unindexed`. These fields describe indexed model, texture, and blockstate resources. They do not establish whether a mod is installed, whether a block is registered, or whether Java generates that resource at runtime. All original failures and reason strings remain visible.

Imported polygon geometry now contributes occupied voxel bounds. This fixes incorrect selection dimensions and a printing assertion for sparse selections containing only imported geometry. Precision regressions exercise thin faces 4,096 blocks apart, final rotation and scaling, individual/mosaic OBJ, actual ASCII STL printing, local USD instancing, float-format preflight, and existing-file preservation. Uncentered world starts at 20,000,001 and 20,000,002 verify both final placement and odd world-to-box offsets. Model centers, camera placement, and offset metadata retain double precision.

The full ATM10 8.3 catalog now contains 45,117 definitions, 584,869 selectors, 31,843 textures, 91,469 geometries, 552,092 elements, 34,642 polygons, and 25 dimensions. This pass adds 91 definitions and 12,115 selectors without losing any previously supported selector. Numeric multipart handling recovers 51 definitions and 11,517 selectors. Four additional definitions still need absent Happy Hallows textures. The storage adapters recover 40 definitions and 597 selectors. The cullface fix recovers the remaining SecurityCraft scaffolding selector.

The report lists 7,034 unsupported definitions and 5,022 unsupported states. The larger state count exposes 3,168 absent-texture combinations inside newly supported multipart definitions; it does not indicate lost prior support. Missing-resource diagnostics identify 4,456 definition failures and 4,392 state failures whose referenced namespace has no indexed resources. Another 497 definition failures and 117 state failures reference namespaces with some indexed resources.

All 153 Python tests passed. Native macOS ASan/UBSan exports passed all 12,115 newly supported selectors across 24 batches. All 6,224 IE part cases also passed in wide rows, including the formerly collapsed chunk-loader faces. Both runs checked source texture pixels, repeated OBJ exports, and exact Sponge geometry/state round trips. The full catalog baseline checked 345 states and 380 exported textures. The app build, capacity, and security regression checks also passed.

## Default material models and disconnected transmitters

Domum Ornamentum `materially_textured` models now export the supplied parent geometry with its original textures, transforms, and UVs. The wrapper's texture fields do not replace the separately baked parent's textures. Saved block-entity material replacements remain unsupported and produce a warning. Explicit empty leaf models inside this verified loader retain their invisible state identity. Mixed multipart states omit invisible children from the shared geometry table.

Mekanism `transmitter` models now export their verified disconnected fallback. The OBJ loader retains exactly the six direction-specific `NONE` groups. It preserves texture aliases, diffuse material colors, UV orientation, blockstate rotation, and resource overrides. Reports warn that live connections, contents, colors, and optional glass are not evaluated. Other OBJ loaders retain their existing visibility behavior.

This pass adds 66 definitions and 1,338 selectors without losing prior coverage. The full ATM10 8.3 catalog contains 45,183 definitions, 586,207 selectors, 31,867 textures, 91,631 geometries, 553,864 elements, 34,774 polygons, and 25 dimensions. The report retains 6,968 unsupported definitions and 5,010 unsupported states.

All 168 Python tests and native macOS ASan/UBSan fixture exports passed. Native full-pack exports covered all 1,338 new selectors across three batches, with source-pixel checks, repeated OBJ output, and exact Sponge geometry/state round trips. The expanded baseline checked 507 states and 503 textures. Portable Windows fixtures cover 1,139 states. The preceding precision/storage commit passed macOS Intel CI and Windows Debug/Release regression tests. Its full-pack Windows Release test also passed.

## Static machine, wire, and furniture models

EnderIO IO overlays now contribute no geometry when their saved configuration is unavailable, matching the shipped fallback. Separate machine-base geometry remains visible. Painted blocks still require saved paint data. Conditional capacitor-bank models remain unsupported because their selected Athena renderer needs separate support.

More Red exports palette-selected nodes and elbows. Reports identify omitted neighbor-dependent line/edge connections and power tint. States with no supplied visible geometry remain unsupported, including through nested composites. MineColonies' verified `blockrackair` rack variant preserves its invisible schematic identity. Resource replacements with actual geometry still render.

Framework open models now use their supplied components, including arbitrary supported element angles, source UVs, and inherited texture overrides. The adapter preserves the loader's custom-geometry inheritance rules and its zero-endpoint UV defaults. Unsupported root transforms and nonstandard rescale combinations fail explicitly. Native tests compare rotated vertices against independently calculated positions.

This pass adds 45 definitions and 1,342 selectors with no lost coverage. The full ATM10 8.3 catalog contains 45,228 definitions, 587,549 selectors, 31,934 textures, 91,712 geometries, 554,218 elements, 34,774 polygons, and 25 dimensions. Unsupported totals are 6,923 definitions and 5,018 states. Newly exposed fully dynamic wire states account for the increased partial-state count.

All 192 Python tests and native macOS ASan/UBSan fixture exports passed. Full-pack native exports checked every new selector across three batches, with source pixels, repeated OBJ, and exact Sponge geometry/state round trips. The baseline checked 656 states and 620 textures. Portable Windows fixtures cover 1,288 states. The preceding Domum/transmitter commit passed macOS Intel CI and Windows Debug/Release regression tests. Its full-pack Windows Release run checked 1,139 states and 596 textures successfully.

## Framed storage, microchips, wall lanterns, and configurable sides

Functional Storage framed blocks now retain their supplied visible child geometry when saved design materials are absent. Child texture scopes and visibility remain independent. Item-render ordering does not suppress world geometry. Microchips export the verified default cube with six uncullable faces and directional base-texture overrides. Their generated geometry ignores supplied application rotation, matching the shipped loader. Reports identify omitted signal overlays and saved customization.

Four Amendments wall lanterns now export complete bodies and supports: vanilla lantern, soul lantern, Forbidden Arcanus deorum lantern, and deorum soul lantern. The adapter respects custom body precedence, preserves source UVs and element pivots, and applies the verified static body pose. Ten compatibility definitions still lack original body assets and fail explicitly. Inherited lantern models retain their parsed support resource. Nested split-model compositions remain unsupported instead of silently dropping child geometry.

IE configurable-side models export the verified NONE setting on all six absolute directions. Their directional textures, face UVs, and cull directions match the shipped generator. Reports identify omitted saved input/output settings. Rotated composites containing fixed-rotation generated models fail explicitly rather than changing their geometry.

This pass adds 37 definitions and 384 selectors with no lost coverage. The full ATM10 8.3 catalog contains 45,265 definitions, 587,933 selectors, 32,009 textures, 91,760 geometries, 554,392 elements, 34,774 polygons, and 25 dimensions. The report retains 6,886 unsupported definitions and 5,017 unsupported states.

All 222 Python tests and native macOS ASan/UBSan fixture exports passed. Sixteen new native cases cover individual/mosaic pixels, directional quadrants, independently calculated lantern geometry, fixed-rotation equality, and Sponge identity. The Windows assertion code also passed against those native exports. Full-pack exports checked all 384 new selectors, repeated OBJ output, and exact Sponge geometry/state round trips. The baseline checked 706 states and 684 textures. Portable Windows fixtures cover 1,338 states. The preceding machine/wire/furniture commit passed macOS Intel CI, Windows Debug/Release regressions, and full-pack Windows Release tests.

## Custom geometry inheritance fixes

Custom geometry now retains the fields captured where its loader is declared. A new explicit loader declaration resets omitted options to that loader's defaults. Plain children retain their parent's parsed geometry. Child textures, visibility, root transforms, and render-type context remain independently overridable where the renderer uses them.

This corrects OBJ/Mekanism/IE options, composite children, storage defaults, More Red tint geometry, Framework components, and other verified loader-owned fields. Domum retains its captured geometry parent separately from the structural parent. Limited barrels ignore custom part overrides in loaderless children. IE OBJ ignores unsupported NeoForge material options. MI defaults use identity geometry, matching their shipped bake methods. Composite item orders accept bounded string child names; exotic Gson coercions remain explicitly unsupported.

The full ATM10 comparison changed exactly 42 selectors across four XyCraft definitions: buffer, isolator, item-filter pipe, and item-sorter pipe. Their OBJ UVs now use the correct explicit-loader default. Every other selector retained identical geometry, rotations, state colors, and texture data. No selectors were added or lost. Catalog totals remain 45,265 definitions and 587,933 selectors.

All 240 Python tests passed. Six native macOS sanitizer cases checked exact asymmetric geometry, source-gradient UV pixels, both texture modes, MI identity rotation, and Sponge identity. The Windows assertion code passed against those native outputs. Full-pack exports checked all 42 corrected selectors, repeated OBJ output, and exact Sponge geometry/state round trips. The baseline checked 710 states and 689 textures. Portable Windows fixtures cover 1,376 states, including every corrected XyCraft selector. Earlier full Windows Debug runs also passed: 1,139 states for the Domum/transmitter batch and 1,288 states for the machine/wire/furniture batch.

## Static power, storage, and Twilight models

Modern Dynamics pipes export their disconnected center with the supplied texture. Transparent pipes retain inward faces and their opposite-face UV mapping. Generated pipes ignore application transforms, matching the shipped loader. Mekanism energy cubes retain the frame and six inactive LEDs, with original UVs and blockstate rotation. Reports identify omitted connections, attachments, side configuration, and stored-energy effects.

Sophisticated Storage barrels retain the default wood, tier overlay, open state, and flat-top geometry. Sophisticated Backpacks use supplied parts, verified default colors, and palette-selected tanks and batteries. Saved recoloring, materials, packing, displayed items, contents, and animation remain unavailable. Optional unresolved texture aliases preserve the loader's fallback. Required missing resources still fail explicitly.

Twilight connected textures export isolated faces with their base and overlay, including verified castle-door tints and the no-world aurora color. Force fields select supplied elements from saved cardinal directions. Their renderer ignores source rotations. Reports identify omitted diagonal joins, neighbor face suppression, position-dependent color, and emission.

Dynamic flag expansion preserves explicit true flags in partial palettes. Omitted flags use verified false defaults. Complete selectors remain more specific. Nested dynamic models require explicit flags, so wrappers cannot silently freeze their default geometry.

This pass adds 35 definitions and 1,266 selectors. Every preexisting selector retains identical geometry, rotations, colors, and texture data. The full ATM10 8.3 catalog contains 45,300 definitions, 589,199 selectors, 32,050 textures, 92,174 geometries, 562,786 elements, 34,828 polygons, and 25 dimensions. Remaining unsupported totals are 6,851 definitions and 5,017 states, including missing compatibility resources.

All 275 Python tests passed. Twenty native macOS ASan/UBSan cases check independently calculated geometry, winding, UVs, RGBA, both texture modes, and Sponge identity. The Windows assertions passed against those native exports. Full-pack native exports checked all 1,266 new selectors, repeated OBJ output, source pixels, and exact Sponge geometry/state round trips. Portable Windows fixtures cover 1,436 sampled states. macOS app, capacity, and security checks passed. Earlier framed-storage and inheritance batches passed full-pack Windows Debug/Release and macOS Intel CI.

## Closed shulkers and isolated pressure tubes

Sophisticated Storage shulker boxes now export their closed base and lid with source UVs, the renderer's slight shrink, six saved facings, and visible tier textures. The propertyless block default faces down. The entity head is not part of the storage renderer. Saved dyes, tier hiding, opening animation, locks, upgrades, and displayed items remain unavailable. Source PNG overrides retain their pixels. Fractional-alpha replacements fail explicitly because the renderer uses cutout materials rather than ordinary alpha blending.

PneumaticCraft pressure tubes now export the initialized isolated appearance: six copies of the supplied disconnected model with the renderer's exact local rotations. Source coordinates, UVs, element rotations, and application rotation remain intact. The raw renderer with absent ModelData draws nothing, so the report explicitly identifies this approximation. Saved closed ends, live connections, modules, and camouflage remain unavailable. Culled child faces are omitted, matching the renderer's null-side query. Unsupported parent-dependent or custom-loader replacements fail explicitly.

Composite root transforms now reject fixed renderer geometry, just as composite application rotations already do. An actual-pack audit found no affected fixed renderer children under nonidentity root transforms. Windows per-block OBJ mosaics now select RGBA and alpha maps for imported textures. A native regression first reproduced alpha changing from 128 to 255, then passed after the exporter fix.

This pass adds nine definitions and 45 selectors without changing any existing selector's geometry, rotation, color, or texture data. The full ATM10 8.3 catalog contains 45,309 definitions, 589,244 selectors, 32,058 textures, 92,213 geometries, 562,789 elements, 35,260 polygons, and 25 dimensions. Remaining unsupported totals are 6,842 definitions and 5,017 states.

All 294 Python tests passed. Twenty-four new native macOS ASan/UBSan cases check analytic geometry, winding, source UVs, both texture modes, tier pixels after nearest-neighbor resampling, and Sponge identity. Full-pack native exports checked all 45 new selectors. The baseline checked 788 states and 749 textures. Portable Windows fixtures cover 1,454 states. Windows assertions also passed against the native renderer outputs. macOS app, capacity, and security checks passed.

## Runtime assets, QIO state fallback, and portable grids

The pinned ATM10 input now includes its matching NeoForge 21.1.259 universal JAR between vanilla and mod assets. Its supplied model parents restore ten FramedBlocks definitions and 64 PneumaticCraft definitions. Downloads remain hash-pinned, and later mod/resource-pack inputs retain precedence. The two loader example blockstates remain unsupported. This corrects an incomplete input set rather than fabricating replacement resources.

Mekanism data-based models now use their supplied `no_data` resource when emitting-redstone ModelData is unavailable. This restores six active QIO adapter states while preserving child textures, rotations, and UV lock. Palette `active=true` does not imply an emitting model. Loader declarations own their `no_data` and `emitting` resource fields.

Refined Storage portable and creative portable grids now retain supplied base geometry, palette activity, and all twelve directions through a verified empty-disk approximation. Partial states preserve supplied properties and use north/inactive defaults for omissions. The renderer replaces application turns with palette facing but retains UV lock. Installed disks, inventory, and separate indicators remain unavailable. Composite children with retained rotations fail explicitly because flattening them would discard their orientation.

This pass adds 76 definitions and 959 selectors without changing any prior selector's geometry, rotations, colors, or texture data. The full catalog contains 45,385 definitions, 590,203 selectors, 32,112 textures, 92,292 geometries, 563,546 elements, 35,260 polygons, and 25 dimensions. Unsupported totals are 6,768 definitions and 5,011 states. These include missing compatibility resources and the newly indexed loader examples.

All 303 Python importer tests and ten downloader tests passed. Twenty new native macOS ASan/UBSan cases check exact geometry, UVs, pixels, both texture modes, and Sponge identity. The Windows assertions passed against those native outputs. Portable Windows fixtures cover 1,594 states. Full-pack native exports checked all 959 new selectors. The baseline checked 928 states and 812 textures, with repeated OBJ output and exact Sponge geometry/state round trips.

## XyCraft pipe caps and closed storage chests

XyCraft pipes now export the verified cache-zero appearance: supplied core geometry plus six caps. The core retains application turns and UV lock. Caps retain their independent direction rotations with UV lock disabled. Original OBJ/composite visibility and texture scopes remain intact. Cull hints are removed because the shipped renderer collects all child face lists. Reports identify omitted live insert, extract, and transfer connections.

Sophisticated Storage chests now export exact closed single, left, and right meshes with saved facing and chest type. Each double-chest cell emits its own half. Propertyless and partial states use verified north/single defaults. Body and lid use the acacia base with the selected tier overlay; the latch uses the tier texture alone. Saved wood, dyes, tier hiding, opening animation, packing, displayed items, locks, upgrades, and inventory remain unavailable.

The asset index now includes `storage_texture_definitions` JSON. Chest materials follow their separate parent hierarchy with per-material overrides. Ambiguous acacia definitions, unsupported ancestor material types, and fractional-alpha cutout replacements fail explicitly. Resource-pack PNG and definition overrides retain normal source precedence.

This pass adds 11 definitions and 128 selectors without changing existing geometry, rotations, colors, or texture data. The full catalog contains 45,396 definitions, 590,331 selectors, 32,155 textures, 92,375 geometries, 563,546 elements, 37,280 polygons, and 25 dimensions. Remaining unsupported totals are 6,757 definitions and 5,011 states.

All 320 importer tests passed. Native macOS ASan/UBSan checks cover 24 new single-cell cases plus a double-chest pair, including exact geometry, UVs, winding, pixels, both texture modes, and Sponge identity. The pair verifies a 30-pixel body width and touching latch halves. Windows assertions passed against those outputs. Full-pack native exports checked all 128 new selectors; the baseline checked 945 states and 830 textures. Portable Windows fixtures cover 1,611 states.

## Immersive Engineering default conveyors

Basic, dropper, and vertical conveyors now export the verified inactive, uncovered fallback mesh. Each uses the shipped renderer's 35 quads, including its separate belt and casing surfaces. Source UVs and replacement PNGs remain intact. Palette facing rotates the model. Propertyless palettes use north. Vertical conveyors retain the exact upright transform and omit the entry belt, which requires neighbor data.

The conveyor loader owns `conveyorType`. Its renderer ignores outer textures, root transforms, application rotations, and UV lock. Nested conveyors require explicit facing. Reports identify omitted saved slopes, covers, dyes, live activity, and connections. Extract, redstone, and splitter conveyors remain unsupported because their extra meshes require a separate orientation contract.

Chest and shulker meshes now count toward the shared compiler geometry budget. Their checks reject an exhausted budget before loading or composing textures. Exact-boundary tests cover both helpers.

This pass adds three definitions and 15 selectors without changing any prior selector's geometry, rotations, colors, or texture data. The full catalog contains 45,399 definitions, 590,346 selectors, 32,159 textures, 92,387 geometries, 563,546 elements, 37,700 polygons, and 25 dimensions. Remaining unsupported totals are 6,754 definitions and 5,011 states.

All 329 importer tests passed. Fifteen native macOS ASan/UBSan cases check every conveyor quad against independent source positions, winding, and UVs. They also verify texture pixels, both export texture modes, and Sponge identity. The Windows assertions passed against those exports, including the four vertical-conveyor bounds. Full-pack native exports checked all 15 new selectors. The baseline checked 960 states and 839 textures, with repeated OBJ output and exact Sponge geometry/state round trips. Portable Windows fixtures cover 1,626 states.

## Supplementaries dry faucet bodies

The faucet loader now exports its supplied off/on body when saved fluid model data is unavailable. It retains application rotations, UV lock, child textures, and cull hints. Both captured children are validated within the normal compiler limits. The liquid child contributes no exported geometry or textures. The importer does not infer a flowing stream from the `has_water` palette property.

Outer faucet textures, visibility, and root transforms do not replace the child model context. Loaderless children cannot override the captured `model` and `liquid` fields. Reports identify omitted saved fluid identity, color, emission, and flowing appearance.

All 64 faucet selectors now compile, recovering eight previously unsupported states. The full catalog contains 45,399 definitions, 590,354 selectors, 32,159 textures, 92,387 geometries, 563,546 elements, 37,700 polygons, and 25 dimensions. No prior selector changed or disappeared. Remaining unsupported totals are 6,754 definitions and 5,003 states.

All 338 importer tests passed. Ten native macOS ASan/UBSan cases verify the two bodies, four facings, combined turns with UV lock, exact geometry/UVs/pixels, retained cull hints, both texture modes, and Sponge identity. The Windows assertions passed against those outputs. Full-pack native exports checked all eight new selectors. Baseline exports checked 962 states and 840 textures, with repeated OBJ output and exact Sponge geometry/state round trips. Portable Windows fixtures cover 1,628 states.

## Canvas signs, Twilight chests, and empty industrial cells

Farmer's Delight standing and wall canvas signs now use their exact entity meshes and supplied 64×32 textures. Standing signs retain all 16 headings and their posts. Wall signs retain the mounting offset and four facings. Propertyless states use verified defaults. Rectangular textures retain normalized UVs through the existing square export slots. Saved front/back text, text dye, and glowing text remain omitted. The 36 orphan Dyenamics compatibility assets remain unsupported. Hanging signs are covered by the following pass.

Twilight Forest's 16 ordinary/trapped chests reuse the verified vanilla single/left/right mesh with their 48 complete entity textures. Facing, half selection, partial states, and north/single defaults remain exact. Live lid opening remains omitted. Explicit visible model replacements bypass these entity fallbacks. The internal `minecraft:builtin/entity` marker comes only from the missing engine parent; source JSON cannot forge or replace it.

The importer now preserves 81 verified Modern/Extended Industrialization fluid-placeholder identities without geometry. These blocks use the intentionally empty `MIFluidBlock` path. The rule requires a curated block identity and the unchanged empty `minecraft:block/air` model. Visible replacements remain visible. Unrelated Create and Copycats air-model placeholders remain unsupported.

This pass adds 131 definitions and 775 selectors without changing prior selector geometry, rotations, colors, or texture data. The full catalog contains 45,530 definitions, 591,129 selectors, 32,224 textures, 92,919 geometries, 563,546 elements, 44,828 polygons, and 25 dimensions. Remaining unsupported totals are 6,623 definitions and 5,003 states. Engine entity placeholders are no longer classified as missing resource files.

All 357 importer tests passed. Native macOS ASan/UBSan checks cover 24 sign cases, 28 chest cases, and mixed visible/invisible schematic identity. They verify exact source geometry, winding, UVs, pixels, defaults, all standing-sign headings, both texture modes, and Sponge round trips. Windows assertions passed against those outputs. Full-pack native exports checked all 775 new selectors. The baseline checked 959 states and 816 textures. Portable Windows fixtures cover 1,727 states.

## Hanging canvas and Twilight signs

Farmer's Delight's 34 hanging-sign definitions now retain their exact boards, mounting planks, and chains. Ceiling signs support all 16 headings and both attachment styles. Wall signs support four facings. Missing properties use the verified defaults, including partial property sets. Zero-depth chain planes retain both nondegenerate faces, opposite winding, and distinct front/back UVs.

Twilight Forest's 16 standing/wall signs reuse the verified vanilla sign mesh with eight registered wood textures. Their palette headings, wall offsets, and default states remain exact. Visible resource-pack model replacements bypass the entity fallback. Both sign families omit saved text, text dye, and glowing text.

Sign textures share a binary-alpha validation cache. Removing a texture during a failed-state rollback also releases its cached PNG reference. Reloading the same texture name validates the replacement bytes again.

This pass adds 50 definitions and 1,128 selectors without changing or losing prior selector geometry, rotations, colors, or texture data. The full catalog contains 45,580 definitions, 592,257 selectors, 32,249 textures, 93,691 geometries, 563,546 elements, 53,900 polygons, and 25 dimensions. Remaining unsupported totals are 6,573 definitions and 5,003 states.

All 372 importer tests passed. Native macOS ASan/UBSan checks cover 42 hanging-sign cases and 18 Twilight sign cases in both texture modes. They verify exact geometry, winding, UVs, pixels, defaults, partial states, and Sponge identity. Windows assertions passed against those outputs. Full-pack native exports checked every new selector. The baseline checked 960 states and 820 textures. Portable Windows fixtures cover 1,828 states.

## Compiled format

The importer writes version 6 for split models. Otherwise, it writes version 5 for repeated UVs, version 4 for Z rotations, version 3 for polygons, or version 2. The runtime reads versions 1 through 6. All versions use UTF-8 text with tab-separated fields. Resource identifiers and selectors cannot contain control characters. Dimension records are optional. Texture and geometry indices each start at zero and increase by one. References must use earlier records.

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

A `geometry` record stores reusable elements and faces. Version 3 also permits `polygon<TAB>texture_index<TAB>vertex_count`, followed by five tab-separated numbers per vertex: `x y z u v`. A polygon has three or four vertices. Positions use pixels within -256..256, and UVs use 0..16 with V downward, extended as described below for version 5. Degenerate polygons are invalid. Polygon limits are 16,384 per geometry and 1,000,000 per catalog. Each `usemodel` references that geometry with its own blockstate rotations and UV-lock setting. A state can reference several models. `endgeometry` and `endstate` close their respective definitions. A new `element` record closes the previous element.

Version 6 adds cell-selection metadata to a state. It retains all version 5 geometry features:

```text
state<TAB>namespace:block[property=value]<TAB>RRGGBB<TAB>alpha_byte
splitpolicy<TAB>policy<TAB>expected_entity_id_or_dash<TAB>master_x<TAB>master_y<TAB>master_z<TAB>size_x
usemodel<TAB>geometry_index<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1[<TAB>z_degrees]
part<TAB>offset_x<TAB>offset_y<TAB>offset_z
emptypart<TAB>offset_x<TAB>offset_y<TAB>offset_z
endstate
```

`splitpolicy` precedes the state's models. Each `part` follows a shared `usemodel`; inline geometry cannot supply a split part. `emptypart` declares a known empty cell without a model reference. Part coordinates must be unique integer triples within -16..16. Master coordinates use the same bounds; `size_x` must be 1..32. A split state cannot mix ordinary models or an `invisible` marker with its parts.

| Policy | Part selection |
| --- | --- |
| `dummy` | Saved integer selects `(0, dummy, 0)`; requires the exact declared entity ID. |
| `position` | Saved `posInMB`, optional X mirroring, then subtraction of the declared master coordinates. The declared master ID selects `(0, 0, 0)`. |
| `post` | `post_slave` supplies Y; `horizontal_offset` supplies the horizontal displacement. |
| `slave_x` | `multiblockslave=true` selects `(1, 0, 0)`; false selects the origin. |
| `slave_y` | `multiblockslave=true` selects `(0, 1, 0)`; false selects the origin. |
| `slave_z` | `multiblockslave=true` selects `(0, 0, -1)`; false selects the origin. |
| `meter` | `multiblockslave=true` selects the origin; false selects `(0, -1, 0)`. |

The first two policies require an entity ID. Palette-only policies use `-` in that field. Older manifest versions reject the new records. Shared geometry remains immutable when the runtime selects a part.

Version 5 permits face and polygon UV coordinates within -256..256 pixels, with at most 256 texture cells in each source face's UV bounding rectangle. Earlier versions retain their 0..16 UV restriction. Expanded faces repeat the source texture through bounded subdivision at export. In-range faces retain their original topology.

Version 4 permits an optional final `z_degrees` field on `usemodel` and inline `model` records. Omission means zero. X, Y, and Z must each be 0, 90, 180, or 270. Z applies first, then X, then Y, about the block center. Versions 1 through 3 reject the additional field.

Version 1 uses `model<TAB>x_degrees<TAB>y_degrees<TAB>uvlock_0_or_1` inside each state, followed by inline elements and faces. It does not use shared geometry records.

Version 2 also permits an explicit `invisible` line between `state` and `endstate`. Such a state must have alpha zero and no models. It keeps a known block's schematic identity without rendering it. Unmarked empty states, mixed invisible/model states, and duplicate markers are invalid.

The state color and alpha describe the map sample. Exported model opacity comes from its face textures.

Element coordinates and rotation origins must be within -256..256 pixels. Inverted bounds preserve the original face winding and UV mapping.

Axis values are `none`, `x`, `y`, or `z`. Face directions are `down`, `up`, `north`, `south`, `west`, or `east`. A cullface uses one of these directions or `-`. Properties appear in alphabetical order. Omit brackets for an empty selector.

Catalog limits are 100,000 block definitions, 1,000,000 selectors, 250,000 shared geometries, 4,000,000 elements, 131,072 textures, and a 512 MiB manifest. These are catalog limits. The active block IDs and selected export textures have the separate limits described above.
