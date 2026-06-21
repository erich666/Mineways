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
"/c/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/MSBuild.exe" \
  "C:/Users/ehaines/Documents/Github/Mineways/Mineways.sln" \
  -t:Mineways -p:Configuration=Release -p:Platform=x64 -p:PlatformToolset=v143 \
  -m -verbosity:minimal -nologo
```

- Warnings are errors (e.g., C4244 narrowing fires the build). Be explicit with
  casts on int→short, especially writes to `block->grid[]` and similar.
- The LNK4099 PDB warnings on `ssleay32MT.lib` / `zlibstat64.lib` are pre-existing,
  unrelated to anything we change. Ignore them.
- If linking fails with `LNK1104: cannot open file '...Mineways.exe'` the running
  binary is locked:
  `Get-Process Mineways -ErrorAction SilentlyContinue | Stop-Process -Force`.
  Always kill it before rebuilding.

Mineways is a GUI app — there is no headless test mode. "Smoke test" = build
clean, launch by hand, exercise the feature you changed.

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

---

## Block-state architecture (the heart of the project)

### `gBlockDefinitions[NUM_BLOCKS_DEFINED]` (blockInfo.cpp / blockInfo.h)

The static per-block-type table. Indexed by Mineways' internal block ID.
`NUM_BLOCKS_DEFINED` is currently 504. Each row has color, alpha, swatch coords,
class flags (`BLF_WHOLE`, `BLF_BILLBOARD`, `BLF_3D_BIT`, etc.). When adding a
new block type:
- Bump `NUM_BLOCKS_DEFINED` in `blockInfo.h`.
- Add the row to `gBlockDefinitions[]` in `blockInfo.cpp`.
- Add the `BLOCK_*` enum value in `blockInfo.h`.

### `BlockTranslations[NUM_TRANS]` (nbt.cpp)

The (Minecraft-name → Mineways `(blockId, dataVal)`) table. `NUM_TRANS = 1178+`
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

### `BlockTranslator.blockId` casts on grid writes

When writing the synthetic test-world geometry (`MinewaysMap.cpp testBlock`),
casts on `block->grid[idx] = type` need to match the grid pointer type. On
master: `(unsigned char)`. On `type_field_short`: `(unsigned short)`.

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
- **Diagnostic logging is welcome** when stuck. Pattern: write to
  `C:\Users\ehaines\cull_debug.log` from inside the dialog/code, ask the user
  to repro, read the log back via `Read`, then strip the logging after.
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
- Memory `~/.claude/projects/.../memory/` directory: I'm supposed to populate
  this with structured memory files. It is currently empty. If something seems
  worth remembering across sessions and doesn't fit in this CLAUDE.md, write
  it there.

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
