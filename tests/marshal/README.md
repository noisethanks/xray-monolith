# lmarshal cross-VM tests

Save data written by scripts through `marshal.encode` (the `.scoc` files) is
encoded by `src/3rd party/lua-extensions/lmarshal.c`. These tests check what
survives a LuaJIT change, which is the save compatibility question behind a
move to 64-bit (GC64) LuaJIT.

`run.sh` builds the vendored LuaJIT 2.0.4, upstream 2.1 GC64 and upstream 2.1
non-GC64, links the engine's `lmarshal.c` into each, and decodes every corpus
case on every VM. It runs on Linux or WSL in about a minute and a half, and in
CI through `.github/workflows/tests-marshal.yml`.

## What it established

- Plain data (numbers including NaN, inf and -0, binary strings, odd keys,
  shared references, cycles, large tables) decodes identically on every pair.
  Encoded bytes differ between VMs because table iteration order differs.
- Stored Lua functions, including `__persist` callbacks, cannot be loaded by
  any other VM. That includes 2.1 GC64 against 2.1 non-GC64. `lmarshal` stores
  them as raw bytecode, and LuaJIT rejects bytecode from another version or
  frame layout.
- Without the `lua_load` check in `lmarshal.c`, such a function silently became
  an error string, broke the decode later, or segfaulted outside pcall. With it,
  decode raises `marshal: stored function cannot be loaded`.

So a save is portable across VMs exactly when it stores no functions.

## Checking real saves

`scan_marshal.py` walks a marshal blob without a Lua VM and lists every stored
function with its path, the VM build that wrote it and the source chunk:

    python3 tests/marshal/scan_marshal.py "<Anomaly>/appdata/savedgames/"*.scoc

Exit code 2 means at least one function was found.
