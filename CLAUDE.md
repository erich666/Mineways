# Mineways — notes for Claude

This file is the durable hand-off between Claude sessions on this project.
Read it before doing anything substantive in this repo.

---

## What this is

Mineways is a Windows GUI app (C++, MFC-ish, Win32) that reads Minecraft world
saves and exports a selected region to OBJ / USD / STL / VRML / Sponge schematic.
The codebase is one big project: `Win/Mineways.vcxproj`. There is also an unbuilt
`Mineways_simplified_Chinese.rc` — out of scope; do not touch.

Maintainer: Eric Haines. Repo lives at `~/Documents/Github/Mineways`.

## Build

```
"<Visual Studio>/MSBuild/Current/Bin/MSBuild.exe" Win/Mineways.vcxproj \
  -p:Configuration=Release -p:Platform=x64 -v:m -nologo
```

(e.g. `C:/Program Files/Microsoft Visual Studio/18/Community/...`; older setups used the 2022
Professional MSBuild on `Mineways.sln` with `-t:Mineways -p:PlatformToolset=v143`.) Build both
`Release` and `Debug`: the Debug build runs the many `assert()`s, which is where most bugs show up.
The Debug build uses AddressSanitizer, so to run it from a shell, the MSVC tools directory holding
`clang_rt.asan_dynamic-x86_64.dll` must be on PATH (e.g. `.../VC/Tools/MSVC/<version>/bin/Hostx64/x64`).

- Warnings are errors (e.g., C4244 narrowing fires the build). Be explicit with
  casts on int→short, especially writes to `block->grid[]` and similar.
- The LNK4099 PDB warnings on `ssleay32MT.lib` / `zlibstat64.lib` are pre-existing,
  unrelated to anything we change. Ignore them.
- If linking fails with `LNK1104: cannot open file '...Mineways.exe'` the running
  binary is locked:
  `Get-Process Mineways -ErrorAction SilentlyContinue | Stop-Process -Force`.
  Always kill it before rebuilding.

### Headless scripting (the way to test)

`Mineways.exe -headless script.mwscript` runs a Mineways script with no window and exits, so an export can be
run, and its OBJ checked, from a shell. The script commands are the ones `interpretImportLine()` and friends parse
in Mineways.cpp (grep `findLineDataNoCase(line, "`); the GUI's File > Export dialog also saves its settings as a
script. A typical test script:

```
Minecraft world: C:\Users\<you>\AppData\Roaming\.minecraft\saves\<world>
Terrain file name: <repo>\TileMaker\TileMaker\terrainExt.png
Set render type: Wavefront OBJ absolute indices
File type: Export individual textures to directory mytex
Center model: NO
Export separate types: YES
Individual blocks: YES
Export lesser blocks: YES
Selection location min to max: 0, 70, 0 to 400, 70, 400
Export for Rendering: C:\temp\out.obj
Close
```

- `Minecraft world:` takes a world folder name in `saves`, a full path, a `.schem`/`.schematic` file, or
  `[Block Test World]` (see below). `Export Schematic: x.schem` writes a Sponge schematic; `Export for 3D Printing:`
  (with `Set 3D print type:`) a print model. Script status and warnings go to stdout/stderr.
- In headless mode an `assert()` pops up a dialog and blocks. To get asserts on stderr instead while testing,
  temporarily put this right after `if (gHeadless) {` in Mineways.cpp, and remove it after:
  `_set_error_mode(_OUT_TO_STDERR); _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);`
  An assert then ends the run with exit code 3 and the file and line on stderr.
- Test the texture output modes separately: "Export individual textures" (`gModel.exportTiles`), "Export full
  color texture patterns" (one mosaic image), and "Create composite overlay faces" take different code paths in
  `getSwatch()`/`getCompositeSwatch()`; so do "Split by block type" (per-state materials and emission levels) and
  3D printing with "Export lesser blocks" on and off.

## Resource files are UTF-16 LE with CRLF

`Win/Mineways.rc` and `Win/resource.h` are UTF-16 LE encoded with CRLF line
endings. The standard `Edit` tool does **not** preserve those — Python on this
machine is also unreliable. The working approach is PowerShell:

```powershell
$lines = Get-Content $path -Encoding Unicode
# ...mutate $lines...
$content = ($lines -join "`r`n") + "`r`n"
[IO.File]::WriteAllText($path, $content, [Text.UnicodeEncoding]::new($false, $true))
```

The `UnicodeEncoding($false, $true)` constructor preserves the BOM and writes
without an extra newline. Verify with `iconv -f UTF-16LE -t UTF-8 $path | grep …`.

## Source files are CRLF

The `Win/*.cpp` and `*.h` files (and `docs/mineways.html`, this file) have CRLF line endings
(`core.autocrlf=true`). Keep them that way: `sed -i` in Git Bash strips the CRs. A reliable way to edit
from a script is Python that reads bytes, works on `\n` text, and writes back `\r\n` (decode as latin-1,
or utf-8 for the html, so non-ASCII bytes survive). Check afterwards that no bare LF crept in. Windows
sometimes holds a file briefly (e.g. Visual Studio or an indexer), so a write can fail with Errno 22;
retry after a moment.

---

## Block-state architecture (the heart of the project)

### `gBlockDefinitions[NUM_BLOCKS_DEFINED]` (blockInfo.cpp / blockInfo.h)

The static per-block-type table. Indexed by Mineways' internal block ID.
`NUM_BLOCKS_DEFINED` is currently 561. Each row has color, alpha, swatch coords,
class flags (`BLF_WHOLE`, `BLF_BILLBOARD`, `BLF_3D_BIT`, etc.). When adding a
new block type:
- Bump `NUM_BLOCKS_DEFINED` in `blockInfo.h`.
- Add the row to `gBlockDefinitions[]` in `blockInfo.cpp`.
- Add the `BLOCK_*` enum value in `blockInfo.h`.

### `BlockTranslations[NUM_TRANS]` (nbt.cpp)

The (Minecraft-name → Mineways `(blockId, dataVal)`) table, `NUM_TRANS`
rows. One row per distinct Minecraft block-state-name variant Mineways
recognizes. Schema:

```
{ hash, blockId, dataVal, "minecraft_name", PROP_FAMILY }
```

- `hash` is computed once on first chunk load.
- `blockId` is the Mineways internal type ID.
- `dataVal` packs subtype + state bits. The high bit (`HIGH_BIT = 0x80`) was
  historically a "type ≥ 256" promotion marker — see "HIGH_BIT history" below.
- `PROP_FAMILY` (e.g., `STAIRS_PROP`, `FENCE_PROP`, `BULB_PROP`) selects the
  read/write arm for state-string parsing/emission.

Lines 442–1638 are the table proper. They are column-aligned (blockId padded
to width 20, dataVal padded to width 23, names start at the same column).
Preserve that alignment when adding rows — see how lines 442–1087 and 1088–1638
match. Long constants like `BLOCK_FLOWER_POT` exceed the column and shift the
row right; that's acceptable ("let it extend").

### PROP families — the state-string round-trip

For Sponge `.schem` export and import, each block-state property (axis, facing,
waterlogged, distance, …) is parsed by an "arm" in `spongeParseStateString`
(nbt.cpp ~line 4500–5200) and emitted by a matching arm in
`spongeBuildBlockStateString` (nbt.cpp ~line 6800–8000+). Adding a new property
family means:

1. Define a new `XXX_PROP` constant (e.g., `BOOKSHELF_PROP`).
2. Add a read arm in `spongeParseStateString` that converts state strings
   (`"facing=east"`, `"distance=3"`) into `dataVal` bits.
3. Add a write arm in `spongeBuildBlockStateString` that converts `dataVal`
   bits back into the alphabetically-ordered properties string. **Properties
   MUST be emitted alphabetically** — Sponge v3 spec requirement.
4. Tag the relevant `BlockTranslations` rows with the new `XXX_PROP`.

The universal `waterlogged` bit (`WATERLOGGED_BIT = 0x40` in nbt.h) is handled
generically — don't re-handle it inside per-family arms.

### Block-state bit layout in `block->data[]`

```
bit 0x80 (HIGH_BIT)        : retired as type-promotion marker (see history).
                             Reserved as real data bit for BLOCK_HEAD and
                             BLOCK_FLOWER_POT (wall/floor for heads, type for pot).
bit 0x40 (WATERLOGGED_BIT) : universal waterlogged flag
bit 0x20 (BIT_32)
bit 0x10 (BIT_16)          : per-block-family subtype bits
bit 0x08 (BIT_8 / SNOWY_BIT)
bits 0x07                  : low data bits (subtype/state)
```

### HIGH_BIT history

The HIGH_BIT-in-data-as-type-promotion convention is **gone** on the
`type_field_short` branch but **still live** on `master` (as of June 2026).

Originally Mineways stored block IDs in `unsigned char` and ran out of bits
when `NUM_BLOCKS_DEFINED` approached 256. The workaround was a 9-bit type
encoding: low byte in `WorldBlock.grid[i]`, the 0x80 bit in `block->data[i]`
acting as the high bit (`type |= 0x100`). The carve-outs were `BLOCK_HEAD` and
`BLOCK_FLOWER_POT`, which legitimately used bit 0x80 of data as a real state bit.

On `type_field_short` the type is a full `unsigned short` throughout:
- `WorldBlock.grid` is `unsigned short *`
- `nbtGetBlocks` / `regionGetBlocks` / `readPalette` etc. all signature-widened
- `BlockTranslator.blockId` widened from `unsigned char` to `unsigned short`
- All 515 `BlockTranslations` rows with `HIGH_BIT |` in dataVal had their
  HIGH_BIT moved into blockId (`+=256`) and stripped from dataVal
- All promotion branches (`if (data & HIGH_BIT) type |= 0x100`) removed
- All synthetic-block writers (test world, ChangeBlock commands) updated
- `#define HIGH_BIT 0x80` is kept — bit position is meaningful as a data bit
  for the BLOCK_HEAD/BLOCK_FLOWER_POT carve-outs, just no longer for promotion

Memory cost on the wide branch: `grid` doubles per chunk (96 KB → 192 KB).
With `INITIAL_CACHE_SIZE = 6000` on x64 the cache grows from ~576 MB to
~1.15 GB at saturation. Acceptable on modern hardware.

If the branch hasn't merged to master yet, **respect which branch you're on**.
On master, removing HIGH_BIT promotion will break >255 block IDs silently.

---

## Terrain atlas: 32 tiles wide (tiles.h, TileMaker, ObjFileManip.cpp)

- `terrainExt.png` is `XTILES` (32) tiles wide and `VERTICAL_TILES` (80) rows tall. `Win/tiles.h` `gTilesTable` has one entry per cell, so
  its index is `col + XTILES*row`. The left 16 columns hold all the older tiles at their original positions; the right 16 are for new tiles.
- A tile entry may set optional `spanX/spanY` (one image covering NxN tiles, e.g., 32x32 image = 2x2 tiles). The anchor cell holds the name; the cells it
  covers are blank "members" with negative spans pointing back to the anchor (`resolveTileAnchor()`). TileMaker copies the whole region (`tileSpan()`).
  Mineways exports such an image whole (individual-texture export writes one file, e.g. `straw_bed.png`) and models address it with the JSON's own
  `uv`s via `saveBoxModelFace()`. Code that looks at tiles one at a time (e.g. `fillMissingTilesFromBuiltIn()`) must treat a member as part of its
  anchor's image, not as a tile of its own.
- In ObjFileManip.cpp, swatch indices are **paged**, not the table index: page 0 is the left 16 columns row by row, page 1 the right 16 columns
  (`TILES_PER_PAGE`). Each page is 16 tiles wide, so all the `swatchLoc + 1`, `+ 16` and wrap-past-column-15 code works as it always has.
  - `SWATCH_INDEX(col,row)` is plain `col + row*16`: an overflowing col wraps to the next row (e.g., `SWATCH_INDEX(14 + (dataVal & 7), 36)`). Use it for literal
    left-page tiles and for computed offsets.
  - `TILE_TO_SWATCH(col,row)` takes a real tile column 0-31 (what tiles.h and `gBlockDefinitions[].txrX` hold). Use it for anything from block/tile data, and add
    offsets *after* it: `TILE_TO_SWATCH(b.txrX, b.txrY) + (dataVal & 3)`. Never put the offset in its col argument, since col >= 16 means the right page there.
  - Convert back with `swatchToCol/Row/TableIndex()` or `TILES_ENTRY(swatchLoc)` when indexing `gTilesTable` or the input terrain image. Loops over `TOTAL_TILES` that use
    the index as a swatch need `TILES_ENTRY(i)`; loops that only read the table (e.g., using `txrX/txrY`) use `gTilesTable[i]`.
- TileMaker's chest/shelf/copper-chest tile runs also wrap at 16 columns (they live in the left half); `poplar_shelf` is pinned at 20-22,0.
- Old 16-wide terrainBase/terrainExt files are detected (height > 3*width) and widened on load, in both TileMaker and Mineways.
- The embedded fallback `Win/terrainExtData.*` is 512x1280 (16px tiles x 32 wide). Regenerate with `TileMaker -i terrainBase.png -nt -t 16`, then dump to C arrays.
- Output texture resolution is `2 * terrain width` (was `4 *` when 16 wide), so the memory use and swatch capacity are about what they were.
- Test: `Mineways.exe -headless script.mwscript` with "Export all textures to three large images"; the process exit crash (0xC0000005) also happens in HEAD, so ignore it.

## Block types above 511 (poplar, etc.)

- The world grid stores a 12-bit type (low 8 bits in `grid[]`, bits 8-11 in the top nibble of `data[]`), so types to 4095 work. `BlockTranslator.blockId` is now `unsigned short`: a row with `blockId` >= 512
  (e.g., `{ 0, 513, 0, "poplar_button", BUTTON_PROP }`) gives the whole type, and needs no `TYPE_HIGH_BIT1`. Rows below 512 are as before (`blockId` plus `TYPE_HIGH_BIT1` for +256). The `.schem` code
  (`spongeParseStateString`, `findSpongeTranslator`, the `type & 0xFFF` tests) handles this too.
- **Never use a type whose low 8 bits are 0 (256, 512, 768, ...)**: it reads as air. 512 is a placeholder row and `BLOCK_AIR_512`.
- A wood that has many block types gets: subtypes of existing types where there is room (log/wood/stripped/planks/slab/sapling/pressure plate/shelf/wall hanging sign/leaves) and new types where there is not
  (stairs, button, door, fence, fence gate, trapdoor, sign, wall sign, hanging sign). For a new wood, mirror poplar: grep `BLOCK_PALE_OAK_` and `BLOCK_POPLAR_` for every place to add cases, and the sign, hanging sign, shelf,
  pressure plate and door getSwatch code for the tiles. A tile in the right half of the terrain image needs `TILE_TO_SWATCH(col,row)`, never `SWATCH_INDEX(col,row)` with col >= 16 (that wraps to the next row).
- Leaf subtype is `dataVal & 0x7` (mangrove 0, cherry 1, pale oak 2, yellow/orange/red poplar 3/4/5). `persistent` is `LEAF_PERSISTENT_BIT` (0x100), not 0x4 or 0x80: the .schem reader treats 0x80 as "type + 256" and
  keeps only 7 bits of dataVal, so persistent is not kept through a .schem (it is not graphical).
- Checks that worked for a new wood: export the 26.3 debug world (it has every poplar state), compare each poplar state's geometry to its pale oak twin (same shape expected), list each material's swatches from
  the OBJ UVs, and round-trip through `Export schematic:` and back.

## Minecraft 26.3 (DataVersion 5023) chunk palettes (nbt.cpp readPalette)

- A block palette is no longer always a list of `{Name, Properties}` compounds. In 26.3 chunks it can be: a **list of strings** (`minecraft:stone`, when no entry needs
  properties); or a **list of compounds** where a state that is not the block's default has `id` (not `Name`) and `properties` (not `Properties`), and a default-state
  entry is a string wrapped in a compound with an **empty tag name** (`{"": "minecraft:stone"}`; 1.21.5+ heterogeneous-list wrapping). Older chunks keep the old form, even
  inside a 26.3 world (chunks convert only when the game loads them), so both are read.
- **A default-state block has no properties at all.** `readPalette` starts every property at false/0, which is right for most blocks but not e.g. `facing` (starts as east; the
  default is north), walls (`up=true`), signs (`rotation=8`), etc. So when an entry has no properties, `defaultStateProperties()` makes up the default properties in memory and runs
  them through the same parser. It first looks the block up in the generated table `Win/defaultStates.h` (every block with properties in the debug worlds), then falls back to
  hand-written rules (`familyFacesNorthByDefault()` etc.) for blocks not in the table, i.e., newer or modded blocks.
- **`Win/defaultStates.h` is generated - do not edit it.** Run `tools/make_default_states.ps1 -OldWorld <26.2 Debug World> -NewWorld <26.3 Debug World>` after fully generating both
  debug worlds (fly to the far corners so every chunk exists). A block's default is the state the older world lists that the newer world does not list with properties. Redo this for
  each new Minecraft version that adds blocks with properties, and check the "note" lines it prints for blocks whose default it could not tell.
- All property variables are now reset at the start of every palette entry. Before, a stale `powered` from an earlier entry made a waterlogged campfire a soul campfire (both use
  0x8), depending on palette order, which differs between 26.2 and 26.3.
- Testing: export the debug worlds' block layer (y 70) from both versions, then `tools/compare_debug_worlds.cs` (load with Add-Type) decodes each state from the chunks and compares its
  exported geometry between the two worlds by position (the OBJ is centred on the selection: world = OBJ + 256). Result on 26.2 vs 26.3: 32,132 of 32,363 states identical; the rest are
  float noise (signs) and per-position random geometry (chorus plant). Debug worlds hold states that cannot occur in play, so it's a stress test, not the goal.

## Culling Scheme system (Win/CullingSchemes.cpp/.h, plus hooks)

User-defined sets of blocks to hide from both map view and exports. Parallel
in spirit to the now-removed Color Scheme system. Persistence: registry under
`HKCU\Software\Eric Haines\Mineways\CullingSchemes`, one `REG_BINARY` value per
scheme keyed `"scheme N"`, plus a `schemeId` DWORD counter.

### Storage

`CullingScheme` struct has `id`, `name[255]`, `culled[NUM_CULL_ENTRIES = 1200]`.
The `culled[]` array is indexed by `BlockTranslations[]` row index (the
public `blockTransIndexFor(type, dataVal)` API in nbt.h does the reverse
lookup). 1200 is sized comfortably above `NUM_TRANS`.

### Runtime lookup

Two-level: `gIsCulledByIndex[NUM_CULL_ENTRIES]` mirrored from the active
scheme's `culled[]` + a `gAnyCulled` flag for O(1) early-out when no scheme is
active. `isBlockCulled(type, dataVal)` is the hot-path predicate.

### "Standard" scheme is not empty

`applyCullingScheme(NULL)` (= the "Standard" menu item = no user scheme) does
**not** clear everything — it seeds `BLOCK_BARRIER` and `BLOCK_STRUCTURE_VOID`
as culled via `seedDefaultCulled()`. Same set is pre-checked in
`CullingManager::Init()` for fresh user schemes. So those two blocks are
invisible in the map and absent from exports unless the user explicitly
unchecks them in a user-defined scheme.

### Hooks

- Map render: `MinewaysMap.cpp:~5152` — culled cells join the seen-empty path.
- Export filter: `ObjFileManip.cpp:~3030` (`filterBox`) — culled blocks become
  `BLOCK_AIR` before geometry emission.
- Bounds pre-pass: `ObjFileManip.cpp:~2714` (`findChunkBounds`) — culled blocks
  don't expand the export bounding box. **Crucial caveat**: this pre-pass runs
  before `gBoxData` is allocated, so it reads `block->data[chunkIndex]` directly
  rather than `gBoxData[boxIndex].data`. Don't change one without the other.

### Editor dialog quirks

The editor's checkbox ListView (LVS_EX_CHECKBOXES) has two known Windows
quirks that the code works around:

1. Reopen-doesn't-show-checks: Setting the check state via *either*
   `LVIF_STATE` in `InsertItem` *or* a follow-up `SetCheckState` alone is
   unreliable. The code does **both** as belt-and-suspenders.
2. With `LVS_EX_FULLROWSELECT` on, `LVHT_ONITEMSTATEICON` is set in `ht.flags`
   for *any* row click, not just clicks on the checkbox itself. To detect a
   real checkbox click (so we don't double-toggle), the `NM_CLICK` handler asks
   the LV for the icon rect via `LVIR_ICON` and checks if the click X is left
   of `iconRect.left`.

If you change anything in the editor dialog, test: reopen an existing scheme
with some checks set (they must show), click block names (must toggle), click
to the right of names in whitespace (must toggle), click the checkbox itself
(must toggle exactly once, not twice), Cancel/X (must discard, OK must save).

### Schemes write into the OBJ/.schem/etc. statistics header

`writeStatistics` in ObjFileManip.cpp emits a `# Culling scheme: <name>` line
after the existing `# Color scheme:` line (now removed on master). The importer
parses `Culling scheme:` lines in `interpretImportLine` (Mineways.cpp) into
`is.cullingScheme`, then `commandLoadCullingScheme` applies it via the menu.

---

## Color Scheme system — REMOVED

The Color Scheme menu, dialogs, ColorSchemes.cpp/h, `gSchemeSelected`, all
`IDM_COLOR*` / `IDD_COLORSCHEME*` / `IDC_SCHEMELIST` etc. defines, the
`schemeSelected` parameter threading through the writers, the
`# Color scheme:` statistics line, and `commandLoadColorScheme` — all removed.
Do not try to add them back unless explicitly asked. Map rendering still uses
the default colors in `gBlockDefinitions` (bootstrapped via
`SetMapPremultipliedColors(0)` at startup + the embedded `initColors()` calls
in render code at MinewaysMap.cpp:362 and :447).

`SetMapPalette` is gone from MinewaysMap.cpp; only `InvalidateMapRenderCache`
remains for the Culling Scheme path to bump `gColormap`.

---

## ObjFileManip.cpp — geometry conventions

### Coordinate system

Mineways uses pixel coords per block, 0..16 along each axis, Y-up. Cube origins
are passed to `saveBoxMultitileGeometry(boxIndex, type, dataVal, top/side/bottom
swatch, markFirstFace, faceMask, rotUVs, minX, maxX, minY, maxY, minZ, maxZ)`.

**Hard constraint**: the (min, max) pixel coords passed in MUST lie in
`[0, 16]`. The function derives texture UVs from those pixel coords and
asserts `u, v ∈ [0, 1]`. Cubes that visually belong above or beside the
block (e.g., copper golem antenna at y=20..24) **must** be emitted at in-block
coords and then translated into final position via a separate `transformVertices`
call.

The copper-golem statue at `BLOCK_COPPER_GOLEM_STATUE` (search "CG_EMIT") shows
the pattern: macros `CG_EMIT` / `CG_TRANSLATE_RECENT` / `CG_ROTATE_BONE` for
respectively emitting an in-block cube, translating recent vertices by a pixel
delta, and rotating recent vertices around a per-bone pivot.

### Rotation idiom (wall banner is the canonical model)

```c
totalVertexCount = gModel.vertexCount;
gUsingTransform = 1;
// ...emit cubes via saveBoxMultitileGeometry...
totalVertexCount = gModel.vertexCount - totalVertexCount;
identityMtx(mtx);
translateToOriginMtx(mtx, boxIndex);          // block origin → world origin
rotateMtx(mtx, 0.0f, angle, 0.0f);            // around block center
translateFromOriginMtx(mtx, boxIndex);        // back
transformVertices(totalVertexCount, mtx);
gUsingTransform = 0;
```

For per-bone rotation around an arbitrary pivot, insert two `translateMtx` calls
between `translateToOriginMtx` and `rotateMtx` to move the pivot to the origin
and back (see `CG_ROTATE_BONE` in the copper golem case).

**Pivot offset gotcha — read this before writing any per-bone rotation.**
`translateToOriginMtx` moves the block's **center** to world origin, not its
corner. Pixel `(8, 8, 8)` is at the origin afterward, not pixel `(0, 0, 0)`. So
the translation needed to move a pivot given in 0..16 pixel coords to the
origin is `(8-px)/16, (8-py)/16, (8-pz)/16`, **not** `-px/16, -py/16, -pz/16`.
Forgetting the `-8` puts the rotation pivot half a block south-west-down of
where you intended — every rotated bone swings around the wrong point, and
the result looks plausibly broken (visually distinct from "nothing happened",
but very wrong). The corrected pattern is in the `CG_ROTATE_BONE` macro.

### Rotation axis intuition for entity geometry

After zRot=π flip in Java models, the Mineways default-facing direction is
**north** (-Z). For a figure standing upright at the block center:
- **X rotation**: pitch — tilts forward/backward (legs/arms swinging on a sagittal axis).
  Positive X rotation tilts the *top* of a vertical bone toward +Z (south, "backward").
- **Y rotation**: yaw — spins around the vertical axis (used for the SWNE facing
  applied to the whole figure at the end).
- **Z rotation**: roll — tilts side-to-side. Positive Z rotation tilts the top
  of a vertical bone toward -X (visually leftward). For a hanging-down arm,
  +Z rotates the hand outward to one side, useful for the STAR pose's
  arms-overhead.

Signs are easy to get wrong — flipping the sign of a single rotation is
usually how the user wants you to iterate when a pose is "close but wrong direction".

### When to rotate from standing vs emit each pose directly

The copper-golem statue case takes both approaches:

- **STANDING / RUNNING / STAR** share a common skeleton (upright body, head on
  top, limbs at standard positions) — these poses are reached by emitting the
  standing geometry and applying small per-bone rotations.
- **SITTING** is structurally different (body squat on ground, legs lying
  flat, arms folded into the lap) — emitting each piece directly at its
  final position is cleaner than rotation-from-standing. Trying to fold
  standing into sitting via rotations produced fragile geometry that
  couldn't be made to look right without lots of compensating translations.

Rule of thumb: if a pose changes the body's height / Y-extent or fundamentally
re-positions limbs (not just swings them), emit directly. If it's a swing /
splay / tilt from standing, rotate.

### `BlockTranslator.blockId` casts on grid writes

When writing the synthetic test-world geometry (`MinewaysMap.cpp testBlock`),
casts on `block->grid[idx] = type` need to match the grid pointer type. On
master: `(unsigned char)`. On `type_field_short`: `(unsigned short)`.

## Matching Minecraft's block models

The aim, block by block, has been to export each block state as Minecraft 26.3's own JSON model draws it:
the same elements, the same texture coordinates (uv), the same turns. The models are in the Minecraft
client jar (`.minecraft/versions/<version>/<version>.jar`): `assets/minecraft/blockstates/*.json` say which
model(s) each state uses, turned by `x`/`y` (with `uvlock`), and `assets/minecraft/models/block/*.json` hold
the elements (follow `parent` for templates).

### The model machinery (ObjFileManip.cpp)

- `ModelElement` / `ModelFace` hold a JSON element as is: `from`/`to` in pixels (may lie outside 0-16), and per face its
  direction, `uv`, `rotation`, a `billboardBack` flag, and a swatch. An element may have a `rotation` (angle, origin, axis
  X/Y/Z via `rotAxis`, `rescale`).
- `saveModelElements(boxIndex, type, dataVal, anchorLoc, elements, count, yAngle, xAngle, uvlock)` saves the elements;
  `saveRotatedModel(..., xAngle, yAngle, uvlock)` also turns the result as a blockstate does (x first, then y). Most blocks
  converted to models use `saveRotatedModel`; many element tables were generated from the JSON (see "Tools" below), with a
  comment saying so. `saveBoxCustomUVVertices` + `saveBoxModelFace(UVLock)` is the lower-level pair for one box.
- Element rotation: Minecraft's positive angle turns the other way from `rotateMtx`, so pass `-angle`. `rescale` stretches by
  `1/cos(angle)` on the two axes perpendicular to the rotation axis. The newer Euler form, `"rotation": {"x":..,"y":..,"z":..}`
  (e.g. hanging signs), is JOML `rotationZYX`: applied x first, then y, then z.
- `uvlock` follows Minecraft's `BlockMath.getUVLockTransform`: a uv point is taken to the point on the face's side of the block by
  that side's default mapping, turned with the block, and mapped back by the default mapping of the side it lands on. Side faces
  turned only about Y keep their uv.
- A flat element (zero thickness) has two faces back to back; the one marked `billboardBack` is output only when
  `gModel.singleSided` is set (the "Double all billboard faces" option, for renderers that cull back faces); otherwise the
  front face alone is output and rendered double-sided. Flat elements are skipped for 3D printing. A face with no area (the
  sides of a flat element, which some JSON lists) must not be output.
- Faces with a `cullface` are dropped by `modelFaceIsCovered()` when a whole opaque neighbor hides them (only for models not turned by x).
- Inverted elements (`from` > `to` on an axis, e.g. the vault's `cage_inverted_faces`) face inward; express them as inward-facing
  flat elements.

### Rules of thumb for coordinates

- Use Minecraft's numbers. Earlier code rounded small nudges (e.g. 2.99, 0.002) to whole texels or lifted things by
  `Z_FIGHTING_BIAS` (0.05 px); where that changed what is seen (lever base, flower beds, lily pad, frogspawn, leaf litter at 0.25 px,
  glow lichen 0.1 px and vines/ladders 0.8 px from the wall), it was changed back to Minecraft's value.
- Very thin two-sided plates (0.002 or 0.01 px thick, e.g. mangrove roots, azalea, big dripleaf edges) are made flat on the block's
  edge, with the inward face as the billboard back, so the two sides don't z-fight.
- `Z_FIGHTING_BIAS` is still used where a face would otherwise sit on another surface, e.g. the beacon's base, the spore blossom's base,
  straw bed frills on the ground.
- Minecraft offsets some plants randomly in X/Z (bamboo, small dripleaf, pointed dripstone/sulfur spike, mangrove propagule, double
  plants...); Mineways does too, with `wobbleObjectLocation()`. Random choices (rotations, chorus plant end caps) are seeded by position
  (`getRand3to1`), so they can't match Minecraft's own picks.
- OBJ vertices are written with `%.8g`: with `%g`'s 6 digits, a vertex at a world coordinate in the hundreds is off by up to ~1/100 px,
  which visibly distorts thin parts like a tripwire string.

### Textures and getSwatch()

- Model code should name its tiles directly (`TILE_TO_SWATCH(gBlockDefinitions[type].txrX, ...)`, `SWATCH_INDEX(col,row)`), **not**
  call `getSwatch()`: for cutout blocks `getSwatch()` calls `getCompositeSwatch()`, which only works when "Create composite overlay
  faces" is on; otherwise it asserts and returns -1, and the -1 crashes later (a pumpkin/melon stem did this).
- A block with `BLF_CUTOUTS` needs a case in `getSwatch()` (even an empty one); the `default:` case asserts on cutout blocks in Debug.
- Watch for swatch mix-ups between similar tiles: several weathered/oxidized copper tiles are stored oxidized-before-weathered in
  tiles.h, and some code had them swapped.

### Billboards (saveBillboardFaces)

Older thin things (vines, lichen, ladders, rails, lily pads, flowers) are "billboards": pairs of faces sharing vertices. With
`singleSided` both are output; otherwise only the first, rendered double-sided, so its *other* side is often the one seen.
A face turned by a blockstate `x` with `uvlock` (vine or lichen under a block, lichen over one) has front and back uv that are not
mirrors of each other; see `underBlockBill`/`overBlockBill` in `saveBillboardFaces`.

## Block states: dataVal bits and the .schem round trip

Each state Mineways keeps lives in `dataVal` bits, set in three places that must agree:
1. the world reader, `readPalette()` in nbt.cpp (the big `switch (tf)` after the property loop);
2. the Sponge `.schem` reader, the `XXX_PROP` arms keyed on property name (around `case BUTTON_PROP: {` in the second big switch),
   plus fix-ups after the loop (e.g. buttons);
3. the `.schem` writer, `spongeBuildBlockStateString()` (properties alphabetical).

When adding a state bit, do all three, and check the geometry code that reads it. Examples added in the 26.3 work: stained glass pane,
wall and tripwire connections in bits 0x100-0x800 (south, west, north, east); floor/ceiling button direction in BIT_32; floor/ceiling
lever "powered" in 0x10 (its 0x8 is which way the handle points); sculk sensor "cooldown" read as active. Data values are 16 bits, with
the type's bits 8-11 in the top nibble, so bits 0x100-0x800 are free for most blocks.

Round-trip test: export a region (e.g. the Debug World row) with `Export Schematic: a.schem`, load `a.schem` as the world, export an OBJ
and `b.schem`. `a.schem` and `b.schem` should be byte-identical, and the two OBJs should match except for position-random blocks (the
loaded schematic sits one block over in X and Z).

## Test worlds

- **Minecraft's Debug World** (y = 70) has every block state, one per cell, which makes it the best check of model matching. It also
  holds states that cannot occur in play (isolated door halves, walls with nothing above, an extended piston with no head, ...), so
  differences there are not always bugs.
- **[Block Test World]** is synthetic, made by `testBlock()` in MinewaysMap.cpp: each 16x16 chunk shows two block types (x 0-7 and 8-15)
  and dataVals 0-15 down Z, two per chunk; in scripts use y -64 to 100. Each block type's case decides what goes in its 8x8 area: a
  dataVal per variant, plus neighbors where they matter (panes, walls, tripwire with hooks, cushions on snow...). Debug builds also show
  the special BLOCK_UNKNOWN/BLOCK_FAKE blocks at the end, so an "unknown block" warning there is expected. Keep each test within valid
  states (e.g. respawn anchor charges 0-4), since Debug asserts on invalid ones.

## Tools

The 26.3 block-by-block matching used two Python scripts, not (yet) in this repo: a checker that, for each Debug World state, works
out Minecraft's quads (elements, rotations, uvlock, offsets) from the jar and compares them with an OBJ exported with individual blocks
and individual textures, reporting texture, uv-orientation and position differences; and a generator that turns a model's JSON into a
C++ `ModelElement` table. If you need them, ask the maintainer.

---

## Working preferences (inferred from past sessions)

- **"Just implement"** is the default. When a task is straightforward, skip the
  plan-first step and just do it. Pause to ask only if there's a real decision
  that depends on the user's intent (memory cost, scope, semantics) — not for
  routine pacing.
- **Terse responses.** End-of-turn summary = 1–2 sentences max. The user reads
  diffs, not narration. State what changed and what's next.
- **Use TaskCreate for multi-step work.** It surfaces progress and helps the
  user track where we are in a long refactor.
- **Mass refactors via PowerShell regex** are acceptable and the user trusts
  them — but verify with grep afterward, and always build to catch silent
  truncation (especially narrowing warnings as errors).
- **Diagnostic logging is welcome** when stuck. Pattern: write to a log file
  (or stderr, when running headless) from inside the dialog/code, repro, read the
  log back, then strip the logging after.
- **The maintainer checks results in-game and commits.** Don't commit; report
  what changed, what was verified, and what to look at in Minecraft.
- **Don't add `#endif` comments, don't reformat unrelated lines, don't add
  emoji** unless explicitly asked.
- **Match existing column alignment in data tables.** The BlockTranslations
  table was carefully realigned (blockId width 20, dataVal width 23) — preserve
  that when adding rows.

---

## Pitfalls to remember

- Running `Mineways.exe` locks the binary; kill before rebuild.
- The Chinese .rc file is not built; don't update it in parallel changes.
- `gBoxData` isn't allocated during the bounds pre-pass — read from
  `block->data[chunkIndex]` directly, not from `gBoxData[boxIndex].data`.
- `LVHT_ONITEMSTATEICON` is set for any row click under `LVS_EX_FULLROWSELECT`
  — use `LVIR_ICON` rect for actual checkbox-area detection.
- `saveBoxMultitileGeometry` pixel coords must be in `[0, 16]` (UV assert).
- Editor dialogs must not call `SetDlgItemText` for fields that fire `EN_CHANGE`
  before the LV is fully set up — order matters in `WM_INITDIALOG`.
- Model code must not call `getSwatch()` for cutout blocks (composite-only path; see "Textures and getSwatch()").
- A new `BLF_CUTOUTS` block needs a `getSwatch()` case, or Debug asserts.
- A new state bit needs the world reader, the `.schem` reader and the `.schem` writer (see "Block states").

---

## Branches

- `master` — production line. HIGH_BIT-in-data type promotion still active.
- `type_field_short` — HIGH_BIT promotion retired, `WorldBlock.grid` is
  `unsigned short *`, `BlockTranslator.blockId` is `unsigned short`. Behavior
  identical to master modulo bug fixes; intended for merge once memory cost
  is acceptable.
- `LightLevel`, `golem`, `write_schem` — older feature branches; check `git log`
  before touching.

If asked to cherry-pick a fix between branches, the pattern is:
```
git checkout <target-branch>
git cherry-pick <commit-sha>
git push origin <target-branch>
```

Conflicts will usually be in `Mineways.cpp` (the file most actively edited
on both lines). Standard `git add` / `git cherry-pick --continue` flow.
