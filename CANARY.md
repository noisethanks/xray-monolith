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
- **Crash handler serialization and thread tagging.** The first thread to fault or
  hit a fatal error owns the crash report. Other threads that fault meanwhile are
  counted and parked, and the report says "N other threads faulted concurrently".
  A fault inside the handler logs one line and terminates. DbgHelp calls are
  serialized. Threads are tagged ("main", "TBB worker N", thread_spawn names; also
  the OS thread name), and the report shows "faulting thread: <tag> (tid <n>)".
  The crash save now runs last, on both fatal errors and unhandled exceptions
  (access violations didn't save before), after the stack trace, minidump and log
  are on disk. It only runs when the fault is on the main thread, and is skipped
  and logged otherwise ("fault on <thread tag>") or when other threads faulted
  concurrently. A 15 s watchdog logs "crash save timed out" and terminates if the
  save hangs; if the watchdog can't be started, the save is skipped. The owner
  tracks its current stage (header, stack trace, cvar dump, minidump, fault count,
  save, message box, terminate) and names it in the re-entry line. The minidump
  path is logged ("minidump written: <path>"), or why it wasn't written. Both
  paths end with TerminateProcess after a final log flush (exit code 1 for fatal
  errors, the exception code for unhandled exceptions) instead of returning to the
  OS, so DLL detach and static destructors don't run with threads parked
  mid-crash. A Lua stack printed for a fault off the main thread is headed "Lua
  stack (main thread, not the faulting thread):". With
  `-dbg`, `dbg_crash_test <n>` makes n TBB workers fault at once,
  `dbg_crash_test main` faults on the main thread, and adding `hang` makes the
  crash save hang to test the watchdog (`dbg_crash_test main hang`).
- **Scheduler and upload instrumentation.** Startup logs "* TBB: version <runtime
  version>, arena concurrency <n>, hardware threads <n>". Every texture load is
  counted process-wide: the async loads queued by `_CreateTexture` and by
  `CTexture::apply_load` reloads (where level-load textures actually load), and
  `DeferredUpload`. With `-dbg`, a "* TexLoad progress" line is logged every 250
  completed loads (done, in-flight now, peak, private bytes), and
  `DeferredUpload` logs a start line and a summary line (texture count, peak
  concurrent loads, private bytes, working set and texture memory before ->
  after, time, upload thread cap). New console variable
  `r__texture_upload_threads` (default 0 = unlimited, today's behavior) caps
  both: the async loads run in a dedicated `tbb::task_arena` of that size, and
  `DeferredUpload` runs its loop in one. A change applies to the async loads
  once none are queued or running, i.e. by the next level load.
