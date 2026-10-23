# Mineways handoff — 2026-09-04

## Current repository state

- Branch: `master`
- The local security-hardening commits were rebased cleanly onto the latest
  `origin/master`, including upstream's Minecraft 26.2 block changes.
- Local tag `v13.00` points to `727a730`.
- The remote `v13.00` tag was not updated. It previously pointed to `c346a08`.
  Force-updating it was blocked pending explicit confirmation that rewriting
  the published tag is acceptable.
- No files under `Win/` were changed in the latest macOS follow-up.

## Completed macOS work

The macOS review backlog is complete, and the obsolete `TODO.md` was removed.
Sketchfab networking is now disabled in the Windows project too, so its absence
on macOS is no longer a parity gap.

Commit `727a730` includes:

1. Cache and schematic-state cleanup on input transitions.
2. `gTargetDepth` synchronization on normal world load.
3. Streaming Import Settings parsing.
4. Coherent unloaded state after failed world/schematic loads.
5. Modern `dimensions/` layout detection.
6. One Makefile version source for About, SaveVolume, plist, and CI releases.
7. `-MMD -MP` header dependency tracking.
8. Strict Culling Scheme hex decoding with initialized storage.
9. UTF-8 byte-length validation.

Changed files in that commit:

- `.github/workflows/macos-build.yml`
- `.gitignore`
- `Mac/ImportSettings.cpp`
- `Mac/MacCullingSchemes.cpp`
- `Mac/Makefile`
- `Mac/MinewaysFrame.cpp`
- `TODO.md`

## Validation evidence

Passed:

- `make -C Mac clean all`
- `make -C Mac app`
- Custom `MINEWAYS_VERSION=13.01` bundle/binary test
- Default `13.0` build restored
- Incremental header dependency rebuild test
- `git diff --check`
- Native GUI startup and Test Block World rendering
- Select All and selection-floor behavior
- Culling Schemes dialog smoke test
- Valid Import Settings script
- Oversized multibyte UTF-8 line rejected with the 1023-byte error
- Real modern world with a `dimensions/` directory loaded successfully
- Invalid world folder produced a load error, cleared loaded state, and caused
  Export Model to report `No world loaded`
- Loading Test Block World afterward recovered normally
- About dialog displayed `Version 13.0`

Temporary test scripts were removed.

The former failed-load cosmetic issue is fixed: the window title, status text,
and map now reset to the unloaded state. Persisted Culling Scheme names, hex
data, IDs, and ID counters are also validated before use.

## Upstream PR

- PR: https://github.com/erich666/Mineways/pull/166
- Eric Haines's latest comment thanked the user for the hardening work, said
  many changes looked useful beyond macOS, and noted he would review/fold them
  in after SIGGRAPH while working on Minecraft 26.2 blocks.
- A response was drafted but not posted:

> Thanks, Eric — glad the changes are useful. No rush at all; enjoy SIGGRAPH!
> Anything outside `Mac/` is shared-core hardening rather than Windows GUI
> work. I’ve also built and smoke-tested the current macOS app with both the
> test world and a real modern world. When you’re ready, I’m happy to separate
> or rebase pieces if that makes them easier to review alongside the 26.2
> block changes.

## LinkedIn notes

This work is suitable for a LinkedIn project or post, described accurately as
an open upstream PR rather than a merged contribution. Suggested top five
skills:

1. C++
2. macOS Development
3. Cross-Platform Development
4. Generative AI (or AI-Assisted Software Development, if available)
5. Software Security
