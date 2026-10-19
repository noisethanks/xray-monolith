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
- **Staging texture accounting and `r__texture_load_staging`.** DX10/11 load
  normal textures as STAGING copies that stay in host memory until their first
  use converts them (`ProcessStaging`). The "* TexLoad progress" line now shows
  how many textures are held that way and their estimated size ("staging S
  textures / B MiB"). Private bytes is replaced by system used physical memory
  ("system used X MiB", from `GlobalMemoryStatusEx`) in the progress and
  `DeferredUpload` lines, since it reads 0 under Wine and misses DXVK's
  allocations. New console variable `r__texture_load_staging` (R3/R4 only,
  default 1 = today's behavior): 0 loads normal textures directly as IMMUTABLE
  shader resources with the view created at load time. Videos (`.ogm`, `.avi`),
  GIFs, `.seq` frames and `$user$` textures never used the staging path and are
  unchanged. It applies to textures loaded after the change; `r__no_ram_textures`
  / `-noramtex` still turns staging off for every load as before.
- **B6: file index fixes.** Cherry-picked from `fix/locator-m-files`.
  `unload_archive` now unregisters every file from the archive, not just the
  first. The file index (`m_files`) is guarded by a reader/writer lock against
  archive mount/unmount racing with lookups from loader threads. The
  multiplayer auth hash now hashes outside that lock, over the same files. With
  `-dbg`, startup logs "* FS auth hash: <hash> (<ms> ms)", so the value can be
  compared across builds against the same gamedata. A thread that takes the
  lock while already holding it asserts ("m_files_lock re-entered on the same
  thread"). Known gap, unchanged: lookups return pointers into the index that
  callers keep after the lock is released.
- **Sam's detail grass instancing.** `sam/detail-grass-instancing-mt`
  (`da03c830`), merged as one `--no-ff` merge commit, so
  `git revert -m 1 <merge>` drops it. DX10/DX11 grass is drawn instanced and
  batched.

### Release notes

- **Sam's grass needs his matching shaders.** The DX10/DX11 grass instancing
  requires modified detail shaders. Install Sam's matching shaders with this
  build.
- **Proton: heavy maps can run out of host memory during cold loads.**
  Lowering texture quality helps. `r__no_ram_textures 1` avoids the crash but
  can drop framerate badly.
- **Delete old TBB DLLs.** Remove any `tbb.dll`, `tbbmalloc.dll` and
  `tbbmalloc_proxy.dll` from the bin folder. Only `tbb12.dll` is needed.
- **Crash saves are only written for crashes on the main thread.** A crash on
  another thread still writes the log and minidump, but skips the save.
