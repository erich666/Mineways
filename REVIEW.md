# Mineways code review

Reviewed on 2026-07-15 at master commit 2c71b2a. This was a static, review-only pass over the world/NBT ingest path, export and geometry engine, Windows application and script interpreter, native macOS front end, and the image/texture tools. Generated data tables and vendored codec internals were sampled around their integration boundaries rather than treated as application logic.

No production code was changed. Findings below are deduplicated across workstreams and ranked by practical impact. Line numbers refer to the reviewed commit.

## Executive summary

The most serious issues are concentrated at trust boundaries:

- NBT and schematic fields control copies, allocations, and loop bounds without consistent validation.
- Export dimensions and image dimensions are frequently multiplied as 32-bit int values before allocation.
- Several allocation paths either overwrite the only pointer with realloc, ignore an error return, or rely on assert in Release builds.
- macOS path-conversion shims assume conversions fit and terminate, while the export preflight can destroy an existing file.
- Script parsing accepts lines larger than several destination fields and invokes secure-CRT constraint handling instead of reporting a script error.

The first remediation pass should introduce checked size-multiplication helpers, length-aware NBT scalar readers, a single checked allocation/error-propagation policy, and centralized UTF-8/wide-path conversion.

## Top 10

| Rank | Finding | Severity | Likelihood | Why it ranks here |
|---:|---|---|---|---|
| 1 | INGEST-01: palette property strings overflow stack buffers | critical | medium | A malformed world chunk can copy up to 65,535 bytes into 100-byte arrays. |
| 2 | INGEST-02: legacy biome length overflows a 256-byte output | critical | medium | A chunk-controlled NBT array length is copied directly into the fixed biome buffer. |
| 3 | INGEST-03: invalid region chunk lengths become huge zlib input sizes | critical | medium | Negative/zero lengths bypass the upper-bound checks and can make inflate read beyond the compressed buffer. |
| 4 | INGEST-04: schematic dimension products overflow on import | critical | medium | Both GUI paths can retain huge dimensions with undersized block/data allocations. |
| 5 | EXPORT-01: legacy schematic writer overflows totalSize | critical | low-medium | Per-axis checks do not protect the volume product; writes can overrun undersized allocations. |
| 6 | IMAGE-01: TGA dimensions overflow the decoded image allocation | critical | low-medium | Header-controlled 32-bit multiplication can wrap before the decoder fills every row. |
| 7 | EXPORT-02: world-box products overflow geometry allocations/indexing | critical | low-medium | Large or scripted selections can wrap the shared dimensions used by allocations and index macros. |
| 8 | EXPORT-03: face-record pool OOM writes past a full pool | high | low-medium | Release builds remove the only assert and continue with an out-of-bounds write. |
| 9 | MAC-01: export preflight truncates and deletes an existing output | high | medium-high | A normal overwrite attempt can lose the old file before the real export starts or validates completion. |
| 10 | INGEST-10: packed block-state counts can bypass buffer validation | high | low-medium | Overflow-prone byte-count checks can admit malformed counts into unchecked decoding arithmetic. |

## Full findings

### World parsing and data ingest

#### INGEST-01 — Palette property names and values overflow 100-byte stack buffers

- Severity: critical
- Location: Win/nbt.cpp:3677-3703
- Trigger: In a flattened chunk palette, a Properties compound contains a token name or string value longer than 99 bytes.

readPalette reads an unsigned 16-bit NBT string length and passes it to bfread for char token[100] and char value[100], then writes the terminator at token[len] or value[len]. Neither length is checked against the destination. A crafted region file can therefore overwrite the stack.

Suggested fix: reject lengths greater than or equal to the destination capacity before reading, and use one helper that reads a length-prefixed NBT string into a caller-supplied span.

#### INGEST-02 — Legacy biome array length can overflow the caller's 256-byte buffer

- Severity: critical
- Location: Win/nbt.cpp:2397-2426
- Trigger: A pre-1.13 chunk advertises a Biomes TAG_Byte_Array length greater than 256.

The old-format branch reads len bytes directly into biome, whose callers provide 16×16 bytes. The newer-format branch validates recognized lengths, but the legacy branch has no equality or upper-bound check.

Suggested fix: require len == 256 for the legacy format. Reject other values before bfread.

#### INGEST-03 — Region chunk length lacks lower-bound and sector-payload validation

- Severity: critical
- Location: Win/region.cpp:97-148
- Trigger: A region header points at a nonempty sector record whose chunk length is zero, negative when decoded into int, or larger than the sector payload after accounting for its header.

chunkLength is assembled into signed int. The code only checks upper bounds, then assigns chunkLength - 1 to z_stream.avail_in and buf + 5 to next_in. A high-bit length can become negative and convert to a huge unsigned zlib length. Zero has the same underflow problem. The current comparison also permits four bytes more than were actually read as compressed payload.

Suggested fix: decode into uint32_t, require sectorNumber > 0, require chunkLength >= 1, and require 4 + chunkLength <= sectorNumber * 4096 and chunkLength - 1 <= CHUNK_DEFLATE_MAX - 5 before configuring zlib.

#### INGEST-04 — Schematic dimensions are multiplied in signed 32-bit arithmetic

- Severity: critical
- Location: Win/nbt.cpp:6870-6888; Win/Mineways.cpp:3715-3735, 3756-3789; Mac/MinewaysFrame.cpp:183-207; downstream indexing at Win/MinewaysMap.cpp:8237-8304
- Trigger: A legacy or Sponge schematic supplies individually valid dimensions whose product exceeds INT_MAX and wraps to a small positive value.

The legacy GUI loaders allocate using the wrapped numBlocks. The Sponge reader repeats the same multiplication before allocating and decoding. The original dimensions are retained and later drive createBlockFromSchematic loops and index calculations; their assertions disappear in Release builds. This can read beyond the undersized block/data arrays.

Suggested fix: validate each dimension and compute the product with checked size_t multiplication in the parser. Reject volumes that exceed INT_MAX, the downstream index type, or an explicit application memory limit. Return the validated count alongside the dimensions so GUI layers do not recompute it.

#### INGEST-05 — Primitive NBT readers ignore short reads

- Severity: high
- Location: Win/nbt.cpp:2014-2127
- Trigger: A truncated gzip-backed level.dat or schematic ends within a word, dword, int, or double.

readWord, readDword, readInt, and readDouble ignore bfread's return. On a short gzip read they consume partially uninitialized stack bytes and return a plausible length/value, which then feeds seeks, copies, and loop counts. BF_BUFFER reads are all-or-nothing, so the behavior differs depending on the input container.

Suggested fix: make scalar readers return success plus an output value, require an exact byte count from bfread, and propagate a parse error at every call site.

#### INGEST-06 — NBT skip arithmetic can overflow and recursive nesting is unbounded

- Severity: high
- Location: Win/nbt.cpp:2129-2248
- Trigger: Malformed array/list counts or deeply nested lists/compounds in otherwise skippable fields.

skipType and skipList multiply attacker-controlled signed counts by 2, 4, or 8 before seeking. bfseek also adds signed offsets before checking the result. Deep compound/list structures recurse without a depth budget, allowing stack exhaustion.

Suggested fix: validate nonnegative counts, use checked 64-bit offset arithmetic against the remaining input length, and enforce a conservative maximum nesting depth and total-tag budget.

#### INGEST-07 — Cache initialization can dereference null or divide by zero after OOM

- Severity: high
- Location: Win/cache.cpp:114-155
- Trigger: Allocation of gBlockCache or every progressively smaller gCacheHistory allocation fails.

gBlockCache's calloc result is not checked. The history allocation loop can reduce gHashMaxEntries to zero and exit with gCacheHistory null. Subsequent cache insertion indexes gBlockCache and can evaluate gCacheN % gHashMaxEntries.

Suggested fix: make cache initialization return failure atomically; do not publish either global until both allocations succeed, and never allow a zero capacity.

#### INGEST-08 — Partial WorldBlock allocation failures leak earlier allocations

- Severity: medium
- Location: Win/cache.cpp:280-306
- Trigger: Allocation of grid, data, or light fails after the WorldBlock or an earlier buffer succeeded.

Each failure returns NULL without freeing previously allocated members. This worsens memory pressure and makes repeated recovery attempts less likely to succeed.

Suggested fix: allocate through a single cleanup path, or construct a zero-initialized object and call block_force_free on failure.

#### INGEST-09 — newNBT leaks the FILE handle if gzdopen fails

- Severity: medium
- Location: Win/nbt.cpp:2051-2068
- Trigger: fopen succeeds but zlib cannot wrap the descriptor.

The function returns a bfFile with a null gz handle but leaves ret.fptr open.

Successful nbtClose calls also use gzflush plus fclose rather than gzclose, leaving zlib's stream state allocated on every gzip-backed NBT open.

Suggested fix: close ret.fptr when gzdopen fails and establish one matching owner for the descriptor and zlib stream. If the existing CRT mismatch prevents gzclose, duplicate the descriptor or rebuild/use a compatible zlib rather than bypassing zlib cleanup.

#### INGEST-10 — Packed block-state count validation and decoding are overflow-prone

- Severity: high
- Location: Win/nbt.cpp:3102-3165, 3439-3447
- Trigger: A malformed BlockStates long-array count overflows bigbufflen * 8 or produces decoder indices inconsistent with the bytes actually read.

readBlockData checks the byte count after signed multiplication, then the decoder derives bit width and byte indices without proving each access is within the received bytes and the fixed MAX_BLOCK_STATES_ARRAY buffer. Carefully chosen large counts can wrap the validation/read length and enter undefined shift/index arithmetic.

Suggested fix: reject counts with bigbufflen < 0 or bigbufflen > MAX_BLOCK_STATES_ARRAY / 8, derive the required long count from the palette and format, and bounds-check every computed index against the exact received byte count.

#### INGEST-11 — Short block and biome names are advanced past their buffers

- Severity: high
- Location: Win/nbt.cpp:1856-1867, 3476-3488
- Trigger: A palette contains a short or non-minecraft-prefixed name.

findIndexFromName evaluates strcmp against name + 10 before checking strlen. Biome parsing similarly accepts lengths as short as two and unconditionally passes thisBiomeName + 10. Both can read beyond the string object.

Suggested fix: validate the minimum length and exact minecraft: prefix before advancing the pointer; support other namespaces without fixed-offset assumptions.

#### INGEST-12 — Sequential cache shrinking can leave buffers smaller than heightAlloc

- Severity: high
- Location: Win/cache.cpp:360-385
- Trigger: grid realloc succeeds but the subsequent data or light realloc fails.

The first buffers are committed immediately, while heightAlloc is updated only after all three operations. On a later failure, the object advertises its old larger height and callers can access beyond the successfully shrunken allocation.

Suggested fix: allocate/copy all replacement buffers before committing any pointer, or update the object's conservative usable height after each successful shrink and keep all members consistent.

#### INGEST-13 — Legacy section arrays accept undersized lengths

- Severity: medium
- Location: Win/nbt.cpp:2653-2695
- Trigger: A legacy BlockLight, Blocks, or Data array is shorter than its canonical section size.

Only upper bounds are checked. In particular, Data reads len bytes into an uninitialized 2,048-byte stack buffer and then expands all 2,048 bytes, consuming uninitialized contents.

Suggested fix: require the canonical lengths for legacy section arrays, or zero-initialize and process only the validated bytes actually read.

#### INGEST-14 — Sparse Sponge fields permit disproportionate allocations

- Severity: medium
- Location: Win/nbt.cpp:6667-6689, 6725-6736, 6852-6858
- Trigger: A tiny schematic declares a very large sparse palette index or byte-array length.

The current sanity limits still permit roughly 256 MB of palette storage and up to 1 GB for encoded block data, independent of validated schematic volume. This is an easy memory-exhaustion path.

Suggested fix: cap palette capacity to supported block-state counts, tie encoded-data limits to the validated voxel count, and enforce a global import-memory budget.

### Export and geometry engine

#### EXPORT-01 — Legacy .schematic writer overflows totalSize and does not check allocations

- Severity: critical
- Location: Win/ObjFileManip.cpp:32935-32968, 33060-33085
- Trigger: A selection has dimensions no greater than 65,535 individually but a product greater than INT_MAX, or allocation otherwise fails.

totalSize = width * height * length is computed as int. Both malloc results are used without null checks while the voxel loops still traverse the full dimensions.

Suggested fix: use checked size_t multiplication, impose a practical volume cap, check both allocations, and abort with MW_WORLD_EXPORT_TOO_LARGE before writing the NBT body.

#### EXPORT-02 — World-box size and index products overflow

- Severity: critical
- Location: Win/ObjFileManip.cpp:2141-2159, 2198-2225, 2517-2535; BOX_INDEX/WORLD_TO_BOX_INDEX at lines 405-406
- Trigger: A very large map or script-defined X/Z selection.

gBoxSizeYZ and gBoxSizeXYZ are int products shared by allocations, initialization loops, face offsets, and index macros. Wrapping can produce an undersized successful allocation rather than the intended too-large error. startNumVerts and biome allocation products have the same pattern.

Suggested fix: validate the complete padded volume with checked size_t/int64_t arithmetic before setting globals. Keep allocation sizes and index calculations in the same validated unsigned type.

#### EXPORT-03 — Face-record pool allocation failures become out-of-bounds writes

- Severity: high
- Location: Win/ObjFileManip.cpp:2215-2217, 12964-13018
- Trigger: Initial or growth allocation fails during a large export.

The initial faceRecordPool allocation is dereferenced without checking. On growth failure, allocFaceRecordFromPool and allocSimplifyFaceRecordFromPool assert but still index count++ in the already-full pool. Assertions are absent in Release builds.

Suggested fix: return allocation failure from pool helpers and propagate it to the export result. Never use assertions as runtime OOM handling.

#### EXPORT-04 — Instance arrays use realloc unsafely and ignore capacity overflow

- Severity: high
- Location: Win/ObjFileManip.cpp:18339-18360, 18398-18418
- Trigger: A USD instancing export grows either list under memory pressure or past an int-sized capacity.

The code doubles int capacities, assigns realloc directly over the original pointer, and dereferences the result unconditionally. Failure loses the old allocation and immediately causes a null write.

Suggested fix: check capacity multiplication, realloc through a temporary pointer, and propagate failure.

#### EXPORT-05 — USD mesh allocation failures are explicitly ignored

- Severity: high
- Location: Win/ObjFileManip.cpp:30024-30038, 30047-30080, 30701-30740
- Trigger: A large USD export cannot allocate output/hash arrays.

allocOutHashData and allocOutData return failure, but callers discard it and continue into outputUSDMesh with null or incomplete arrays.

Suggested fix: check both returns and unwind with MW_WORLD_EXPORT_TOO_LARGE before emitting the mesh.

#### EXPORT-06 — Auxiliary geometry allocations are inconsistently checked

- Severity: medium
- Location: representative sites Win/ObjFileManip.cpp:15880, 16020, 16271, 16740, 16806, 17714-17717, 17857, 17987, 18276, 25283-25311
- Trigger: Memory pressure during flood fill, hollowing, touch-grid construction, unused-vertex removal, or UV generation.

Several malloc/calloc results are immediately passed to memset or indexed. Core vertex/face growth helpers already have a checked pattern, but auxiliary algorithms do not follow it.

Suggested fix: standardize these helpers on one checked allocation API and a common export cleanup/error path.

#### EXPORT-07 — Empty numeric fields can bypass export-dialog validation

- Severity: medium
- Location: Win/ExportPrint.cpp:1454-1479
- Trigger: A user clears one numeric field while later fields parse successfully.

sscanf_s returns EOF (-1) for empty input. Combining results with bitwise &= lets a later successful result leave nc nonzero, preserving a stale value and bypassing the error dialog.

Suggested fix: require every conversion result to equal 1 and report the specific invalid field.

#### EXPORT-08 — gzdopen failure leaks open schematic files

- Severity: medium
- Location: Win/ObjFileManip.cpp:32955-32968, 33427-33436
- Trigger: fopen succeeds but gzdopen fails.

Both legacy and Sponge writers return without closing fptr.

Suggested fix: fclose before returning, or use one owner/cleanup label for FILE and gz handles.

#### EXPORT-09 — World metadata helpers use inconsistent 300-character path buffers

- Severity: medium
- Location: Win/MinewaysMap.cpp:8337-8348, 8368-8379, 8402-8414, 8429-8456
- Trigger: A valid world path plus suffix exceeds the fixed buffer.

The secure wide-string calls can invoke the invalid-parameter handler instead of returning a normal load error. Other Mineways paths use MAX_PATH_AND_FILE.

Suggested fix: build paths with dynamically sized strings or a checked shared path helper and propagate truncation as an error.

### Image and texture pipeline

#### IMAGE-01 — TGA image size wraps before vector allocation

- Severity: critical
- Location: TileMaker/TileMaker/readtga.cpp:84-87; decoding loop in tga_decoder.cpp:127-201
- Trigger: A TGA header's width, height, and bytes-per-pixel imply more than UINT32_MAX decoded bytes.

rowstride is uint32_t and rowstride * height is evaluated in 32-bit arithmetic before conversion to vector's size_type. The decoder then writes every declared row using the unwrapped dimensions.

Suggested fix: use checked size_t multiplication for rowstride and total bytes, cap decoded dimensions/bytes, and reject before allocation.

#### IMAGE-02 — Allocated progimage_info objects are never deleted

- Severity: high
- Location: Win/rwpng.cpp:153-180; repeated callers in ChannelMixer/ChannelMixer/ChannelMixer.cpp and TileMaker/TileMaker/TileMaker.cpp; unconditional leak at TileMaker.cpp:2784-2811
- Trigger: Normal processing of texture channels or conversion of a heightfield.

allocateGrayscaleImage and allocateRGBImage use new. writepng_cleanup only clears image_data, and callers do not delete the object. convertHeightfieldToXYZ does not even call cleanup. Batch processing leaks one or more objects per texture.

Suggested fix: return values/unique_ptr, or make ownership explicit and delete after cleanup. The vector needs no separate cleanup before object destruction.

#### IMAGE-03 — Derived PNG allocation sizes use signed int multiplication

- Severity: high
- Location: Win/rwpng.cpp:158-180
- Trigger: Decoded width × height (or ×3 for RGB) overflows int.

The expression overflows before resize receives size_t. Later pixel loops retain the original dimensions and can write beyond the vector.

Suggested fix: validate positive dimensions and use checked size_t multiplication before resize.

#### IMAGE-04 — Tile atlas pixel reads lack runtime bounds checks

- Severity: high
- Location: TileMaker/TileMaker/TileMaker.cpp:2078-2246, 2291-2301
- Trigger: A resource-pack texture has an unexpected size or computed zoom/source offsets fall outside it.

getPNGPixel directly forms a pointer from row and col. The partial assertion in copyPNGTile is absent in Release and does not cover every scaling branch.

Suggested fix: validate source rectangles once before each copy and make getPNGPixel reject out-of-range coordinates.

#### IMAGE-05 — Error formatting uses unbounded wsprintf

- Severity: medium
- Location: TileMaker/TileMaker/TileMaker.cpp:1928-1998; ChannelMixer/ChannelMixer/ChannelMixer.cpp:1756 onward
- Trigger: An unexpectedly long input path reaches reportReadError.

Twenty-nine call sites format into a fixed wchar_t[1000] without a capacity argument.

Suggested fix: use swprintf_s or a dynamically sized string and preserve truncation/error information.

#### IMAGE-06 — PNG header loading masks file-I/O errors

- Severity: low
- Location: Win/rwpng.cpp:75-108
- Trigger: lodepng::load_file fails.

The return code is ignored, so lodepng_inspect reports an empty/corrupt PNG rather than the actual open/read error.

Suggested fix: return the load_file error before inspection and map it accurately in the UI.

#### IMAGE-07 — Unsupported TGA/PNG channel counts fall through to misleading behavior

- Severity: low
- Location: TileMaker/TileMaker/readtga.cpp:217-229; Win/rwpng.cpp:115-150
- Trigger: Future code passes an unsupported pixel/channel format in Release.

The TGA default case falls through to RGB after an assertion, and writepng returns error 1, which callers describe as an invalid PNG signature.

Suggested fix: return an explicit unsupported-format error from all builds.

### Windows application and script parser

#### SCRIPT-01 — Long script values can terminate the process through strcpy_s

- Severity: high
- Location: Win/Mineways.cpp:7131, 7274, 7364, 7581, 7587, 7593, 8337, 8734
- Trigger: A script/import line supplies a path, culling scheme, tile directory, Sketchfab token, or log filename longer than its destination.

readLine accepts up to 1,023 bytes, while destinations range from roughly 32 to 520 bytes. These calls perform no preflight length validation. The secure CRT constraint handler is not a substitute for a recoverable parser error.

Suggested fix: validate each field against its actual destination, reject it with saveErrorMessage during the syntax pass, and prefer a size-aware copy helper.

#### SCRIPT-02 — Invalid booleans are reported as unmatched commands

- Severity: medium
- Location: Win/Mineways.cpp:9061-9092
- Trigger: A recognized toggle command has a non-boolean value.

One validBoolean failure returns INTERPRETER_FOUND_ERROR as bool without storing it through pRetCode. The caller sees FOUND_NOTHING_USEFUL, retries another dispatcher, and can emit duplicate or misleading syntax errors.

Suggested fix: set *pRetCode = INTERPRETER_FOUND_ERROR and return true, matching the adjacent error path.

#### SCRIPT-03 — ChangeBlockCommand cleanup leaks every list node

- Severity: high
- Location: Win/Mineways.cpp:9452-9460
- Trigger: Clearing commands, rerunning scripts, persistent stdin use, or shutdown.

deleteCommandBlockSet frees fromDataBitsArray but never frees the ChangeBlockCommand itself.

Suggested fix: save next, free nested storage, free the node, then advance.

#### SCRIPT-04 — cleanseBackslashes uses strcpy_s on overlapping ranges

- Severity: medium
- Location: Win/Mineways.cpp:9642-9653
- Trigger: A parsed path contains doubled backslashes.

The destination begins one byte before the source, violating strcpy_s's non-overlap requirement.

Suggested fix: use memmove with the terminator-inclusive remaining length.

#### SCRIPT-05 — Select maximum height can reuse unrelated global state

- Severity: medium
- Location: Win/Mineways.cpp:8688-8707
- Trigger: A script sets only the maximum height while gTargetDepth does not match the current selection minimum.

GetHighlightState returns miny, but the command discards it and passes gTargetDepth to SetHighlightState.

Suggested fix: preserve the current miny returned by GetHighlightState, or document and enforce the invariant centrally.

#### SCRIPT-06 — stdin lines are silently split at the fixed buffer boundary

- Severity: medium
- Location: Win/Mineways.cpp:6256-6273 versus file reader at 6878-6900
- Trigger: An interactive/headless command exceeds IMPORT_LINE_LENGTH - 1.

The file reader detects overlong input; readStdinLine returns the prefix as a command and leaves the suffix to be parsed as the next command.

Suggested fix: consume through newline, report one overlong-line error, and execute none of the fragments.

#### SCRIPT-07 — Several error paths return no useful diagnostic

- Severity: low
- Location: Win/Mineways.cpp:8794, 8814, 8870, 8875, 9012, 10245-10298
- Trigger: Out-of-range block/biome commands, OOM, or log write failure.

The functions return INTERPRETER_FOUND_ERROR without saveErrorMessage, yielding an empty or generic error.

Suggested fix: attach a specific message before every parser error return and make error creation part of the handler contract.

#### SCRIPT-08 — Location dialog can report success after validation followed by Cancel

- Severity: low
- Location: Win/Location.cpp:88-117
- Trigger: Enter invalid coordinates, click OK, then Cancel.

gLocOK is set before validation and is not cleared in the Cancel branch.

Suggested fix: set success only after parsing succeeds and reset it on every Cancel/close path.

### macOS wxWidgets port and compatibility layer

#### MAC-01 — Export preflight truncates and deletes the requested output path

- Severity: high
- Location: Mac/MinewaysFrame.cpp:1002-1016
- Trigger: Export to an existing path, especially when the later export fails or the path was typed directly.

The permission probe opens the real output path with wb, closes it, and removes it before SaveVolume runs. Existing data is destroyed before the real export succeeds, and manually typed paths can bypass the file dialog's overwrite prompt.

Suggested fix: probe a uniquely named temporary file in the destination directory. For replacement, write the real output to a sibling temporary file and atomically rename after success.

#### MAC-02 — Imported tile directory may not be null-terminated

- Severity: high
- Location: Mac/ImportSettings.cpp:563-567
- Trigger: An imported header/script supplies at least MAX_PATH - 1 bytes.

strncpy does not append a terminator when truncated, and later validators/path builders treat tileDirString as a C string.

Suggested fix: reject overlong values or explicitly terminate; prefer a shared checked copy routine.

#### MAC-03 — Wide/multibyte path conversions ignore errors and truncation

- Severity: high
- Location: Mac/compat.h:187-195, 221-225, 256-281, 293-327; representative Mac/MinewaysFrame.cpp and Mac/ExportDialog.cpp call sites
- Trigger: A path approaches the fixed conversion buffer or contains an unconvertible sequence.

wcstombs and mbstowcs may fail or fill the destination without a terminator. Callers ignore the return and immediately use the buffer as a C string. This affects file opening, directory creation, attributes, and enumeration.

Suggested fix: centralize conversion with wxString UTF-8 APIs or a checked helper that returns failure, guarantees termination, and dynamically sizes output.

#### MAC-04 — Persisted fileType indexes arrays before validation

- Severity: high
- Location: Mac/MinewaysFrame.cpp:114-130; Mac/ExportDialog.cpp:173, 240, 472-545
- Trigger: A corrupt or edited wxConfig blob contains fileType outside 0..FILE_TYPE_TOTAL-1.

LoadFromEfd indexes many per-file-type arrays without clamping. The constructor also passes negative values to SetSelection. SaveToEfd has the missing validation pattern, but it runs later.

Suggested fix: validate all persisted enum/range fields when decoding the blob and clamp defensively at every array boundary.

#### MAC-05 — Export Map allocation has unchecked dimension products

- Severity: medium
- Location: Mac/MinewaysFrame.cpp:1294-1309
- Trigger: A large selection and/or zoom factor.

width/height assignments multiply as int, while vector size is unbounded and may throw. The address of image_data[0] is taken without an explicit empty/allocation-failure path.

Suggested fix: checked size_t arithmetic, a practical pixel/byte cap, and bad_alloc handling with a user-facing error.

#### MAC-06 — Win32 file-size shims truncate files at 4 GiB

- Severity: medium
- Location: Mac/compat.h:228-251
- Trigger: A large export or ZIP input/output reaches 4 GiB.

GetFileSize, ReadFile, and WriteFile expose only DWORD sizes; GetFileSize casts ftell's result and ignores the high word. Shared ZIP code can therefore see a truncated length.

Suggested fix: provide 64-bit file-size/offset wrappers and explicitly reject formats or APIs that cannot represent the size.

#### MAC-07 — Tilde expansion differs across the Porta file wrappers

- Severity: low
- Location: Mac/compat.h:254-282
- Trigger: A literal ~/ path is created and later opened or appended.

Only _mwPortaCreateW expands the home directory.

Suggested fix: normalize paths once before all three operations.

#### MAC-08 — Script/import text decode failures are swallowed

- Severity: low
- Location: Mac/ImportSettings.cpp:932-942
- Trigger: Invalid UTF-8 or a read failure.

ReadAllLines ignores wxFile::ReadAll's boolean result and can treat failed/partial decoding as an empty file.

Suggested fix: check the result and report I/O versus encoding failures distinctly.

## Cross-cutting remediation order

1. Harden NBT and schematic ingest first: checked scalar reads, destination-aware string reads, dimension validation, and parser depth/tag budgets.
2. Add checked multiplication helpers for dimensions, counts, byte sizes, and capacity doubling; use them before every allocation and index-global assignment.
3. Replace assert-only/ignored allocation failures with one propagated export error and centralized cleanup.
4. Make path conversion and path joining dynamic and fallible on macOS; remove the destructive export probe.
5. Make script field schemas carry destination limits and validation functions so both file and stdin parsers behave identically.
6. Add targeted malformed-input tests/fuzz harnesses for region/NBT/TGA parsing and boundary tests for schematic/export volume calculations.

## Review limitations

This was static analysis; no fuzzing corpus, Windows Release build, or GUI smoke test was run because the task was review-only and no code was changed. Findings involving allocation failure and multi-gigabyte dimensions should still be fixed, but their observed runtime symptoms depend on allocator/CRT behavior. Third-party lodepng internals and the generated terrainExtData table were not line-by-line reviewed; their Mineways-facing allocation, path, and error-handling boundaries were reviewed.
