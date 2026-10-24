# Canary builds

Canary builds are prereleases produced by `.github/workflows/release.yml`. Each one
is tagged `canary-<run number>` at the exact commit it was built from, and logs its
identity on startup and in crash reports:

    * Build: canary-12 (a1b2c3d) canary_10_19

Local builds log `local` in place of the canary number. Sections are newest first.

## release/canary_10_23

Based on upstream `all-in-one-vs2022-wpo-mt` at `871e2491` (10.11 merged into MT).
Rebuilt from upstream rather than from `canary_10_19`. The crash handler
serialization and the texture load instrumentation from `canary_10_19` are not
included.

- **Vendored oneTBB 2023.0.0.** Headers, import libs and runtime DLLs under
  `src/3rd party`, `sdk/libraries/x64` and `sdk/binaries`.
- **oneTBB task scheduler, no allocator.** The `xr_task_group`, `xr_parallel_*` and
  concurrent container aliases in `_thread_types.h` now use oneTBB instead of PPL.
  Only `tbb12.dll` is deployed and shipped. The tbbmalloc scalable allocator is
  intentionally not included.
- **Canary plumbing.** A pre-build step generates `build_identity.h` (git SHA with
  `-dirty`, branch, canary number from `CANARY_NUMBER`). The build line is logged
  at startup, in the crash log header and as a minidump comment. The release
  workflow tags `canary-<run number>` at the built SHA as a prerelease.
- **B6: file index fixes.** `unload_archive` now unregisters every file from the
  archive, not just the first. The file index (`m_files`) is guarded by a
  reader/writer lock against archive mount/unmount racing with lookups from
  loader threads. The multiplayer auth hash now hashes outside that lock, over
  the same files. With `-dbg`, startup logs "* FS auth hash: <hash> (<ms> ms)",
  and a thread that takes the lock while already holding it asserts. Known gap,
  unchanged: lookups return pointers into the index that callers keep after the
  lock is released.
- **DX10 builds.** DX10 and DX10-AVX now deploy `tbb12.dll` like DX11.
- **Sam's grass optimizations (PR #19).** Squashed as one commit authored by Sam,
  from his net diff against upstream MT, so `git revert <commit>` drops it.
  DX10/DX11 grass is drawn instanced and batched, with instances packed once per
  frame, compressed to 64 bytes and depth sorted. Requires the five matching
  grass shaders listed in `packaging/shader-files.txt`.

### Release notes

- **Install.** Extract the main zip into the Anomaly folder and allow overwrites.
  It contains `bin` (executables and `tbb12.dll`) and
  `db/mods/00_modded_exes_gamedata.db0`, so Modded Exes does not need to be
  installed first. Then install the shader zip in Mod Organizer 2 and enable it
  after SSS and other shader mods. Clear the shader cache before launching.
- **Delete old TBB DLLs.** Remove any `tbb.dll`, `tbbmalloc.dll` and
  `tbbmalloc_proxy.dll` from the bin folder. Only `tbb12.dll` is needed.
- **Proton: heavy maps can run out of host memory during cold loads.**
  Lowering texture quality helps. `r__no_ram_textures 1` avoids the crash but
  can drop framerate badly.
