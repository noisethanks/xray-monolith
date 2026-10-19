# Canary builds

Canary builds are prereleases produced by `.github/workflows/release.yml`. Each one
is tagged `canary-<run number>` at the exact commit it was built from, and logs its
identity on startup and in crash reports:

    * Build: canary-12 (a1b2c3d) canary_10_19

Local builds log `local` in place of the canary number. Sections are newest first.

## canary_10_19

Based on upstream `all-in-one-vs2022-wpo-mt` at `871e2491`.

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
