#!/usr/bin/env bash
# Cross-VM test for the engine's lmarshal.c (save data in .scoc files).
#
# Builds three LuaJIT VMs, links src/3rd party/lua-extensions/lmarshal.c into each,
# encodes corpus.lua on every VM and decodes on every VM, one process per decode.
#   lj20      the vendored LuaJIT 2.0.4 sources (what ships today)
#   lj21      upstream LuaJIT 2.1, GC64 (the 64-bit migration target)
#   lj21nogc  upstream LuaJIT 2.1, GC64 disabled (separates the version change from GC64)
#
# Expectations, any violation fails the run:
#   data cases        PASS on every encode/decode pair
#   fn_* same VM      PASS
#   fn_* cross VM     DECODE_ERROR naming the unloadable function, never a crash or silent mismatch
#
# usage: tests/marshal/run.sh            (Linux or WSL, needs gcc, make, git, python3)
set -u
H=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$H/../.." && pwd)
W="${MARSHAL_WORK:-$H/work}"
V="$REPO/src/3rd party/luajit-2/src"
M="$REPO/src/3rd party/lua-extensions"
LJ_URL=https://github.com/LuaJIT/LuaJIT.git
LJ20_BUILD_SHA=2410b57fd466298e6a64a68986a6b696c23656ac  # v2.0 branch, used only for its Makefile and host tools
LJ21_SHA=a4c064539ea931274d407e1249b4bb575424a0f4        # v2.1 branch
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$W"; cd "$W"

fetch() {  # fetch <dir> <sha>
  [ -d "$1/.git" ] && return
  git init -q "$1" && git -C "$1" fetch -q --depth 1 "$LJ_URL" "$2" && git -C "$1" checkout -q FETCH_HEAD
}
build_lj21() {  # build_lj21 <dir> [XCFLAGS]
  [ -f "$1/src/libluajit.a" ] && return
  fetch "$1" "$LJ21_SHA" && make -C "$1" -j"$JOBS" -s ${2:+XCFLAGS=$2} >"$1.log" 2>&1 || { cat "$1.log"; exit 1; }
}
build_lj20() {  # upstream v2.0 build system with the vendored sources copied over it
  [ -f lj20/src/libluajit.a ] && return
  fetch lj20 "$LJ20_BUILD_SHA" || exit 1
  cp "$V"/*.c "$V"/*.h "$V"/*.dasc lj20/src/ 2>/dev/null
  cp "$V"/host/*.c "$V"/host/*.h lj20/src/host/ 2>/dev/null
  cp "$V/luajit.h" lj20/src/luajit_rolling.h
  # Keep the vendored luajit.h, and build the profiler files the vendored copy backports from 2.1.
  sed -i 's/^luajit.h:.*/luajit.h:/; /luajit_relver.txt/d; s/lj_alloc.o /lj_alloc.o lj_allocprof.o lj_profile.o /' lj20/src/Makefile
  make -C lj20 -j"$JOBS" -s >lj20.log 2>&1 || { cat lj20.log; exit 1; }
}

build_lj20; build_lj21 lj21; build_lj21 lj21nogc -DLUAJIT_DISABLE_GC64
VMS="lj20 lj21 lj21nogc"
for v in $VMS; do
  gcc -O1 -g -DluaL_reg=luaL_Reg -I$v/src -I"$M" "$H/driver.c" "$M/lmarshal.c" $v/src/libluajit.a -lm -ldl -o mrun_$v || exit 1
  printf '%-9s %s\n' $v "$(./mrun_$v /dev/stdin <<<'local ffi=require"ffi" print(VM_VERSION, "GC64="..tostring(ffi.abi("gc64")))')"
done

cd "$H"
CASES=$("$W/mrun_lj20" /dev/stdin <<<'dofile("corpus.lua") print(table.concat(ORDER," "))')
for v in $VMS; do rm -rf "$W/out_$v"; mkdir -p "$W/out_$v"; "$W/mrun_$v" encode.lua "$W/out_$v" || exit 1; done

fail=0; total=0
for e in $VMS; do for d in $VMS; do for c in $CASES; do
  r=$("$W/mrun_$d" decode.lua "$W/out_$e" "$c" 2>&1); rc=$?
  [ $rc -ge 128 ] && r="CRASH (signal $((rc-128)))"
  if [[ $c == fn_* && $e != "$d" ]]; then
    [[ $r == "DECODE_ERROR marshal: stored function cannot be loaded"* ]] && ok=1 || ok=0
  else
    [[ $r == PASS ]] && ok=1 || ok=0
  fi
  total=$((total+1)); [ $ok = 1 ] && verdict=ok || { verdict=FAIL; fail=$((fail+1)); }
  printf '%-4s %-9s -> %-9s %-18s %s\n' $verdict $e $d $c "${r:0:140}"
done; done; done > "$W/matrix.txt"
grep -v '^ok ' "$W/matrix.txt"

# The scanner must see every stored function and identify the VM that wrote it.
scan_fail=0
for v in $VMS; do
  want=$(case $v in lj20) echo "LuaJIT 2.0";; lj21) echo "LuaJIT 2.1 GC64";; lj21nogc) echo "LuaJIT 2.1 non-GC64";; esac)
  out=$(python3 scan_marshal.py "$W"/out_$v/*.bin); src=$?
  n=$(grep -c "FUNCTION .*\[$want," <<<"$out")
  if [ $src -ne 2 ] || [ "$n" -ne 7 ] || grep -q "PARSE ERROR" <<<"$out"; then
    echo "FAIL scanner on $v: exit $src, $n of 7 functions identified as '$want'"; scan_fail=1
  fi
done

echo "matrix: $((total-fail)) / $total as expected. scanner: $([ $scan_fail = 0 ] && echo ok || echo FAIL)"
[ $fail = 0 ] && [ $scan_fail = 0 ]
