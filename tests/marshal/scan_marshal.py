#!/usr/bin/env python3
"""Walk an lmarshal blob (e.g. an Anomaly .scoc file) without a Lua VM.

Reports every Lua function stored in the data (plain closures and __persist
callbacks), its path in the table tree, the bytecode flavour that wrote it,
and the source chunk it came from. Data-only blobs are VM independent, so the
function list is the full cross-VM exposure of a save.

usage: scan_marshal.py FILE [FILE...]   (exit code 2 if any function found)
"""
import struct
import sys

T_NIL, T_BOOL, T_NUM, T_STR, T_TAB, T_FUN, T_USR = 0, 1, 3, 4, 5, 6, 7
TREF, TVAL, TUSR = 1, 2, 3
MAGIC = 0x8E


class Blob:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise ValueError(f"truncated at offset {self.p} (need {n})")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]


def uleb(buf, i):
    v = sh = 0
    while True:
        b = buf[i]
        i += 1
        v |= (b & 0x7F) << sh
        sh += 7
        if b < 0x80:
            return v, i


def bytecode_info(bc):
    """Identify the LuaJIT build that produced a lua_dump blob."""
    if len(bc) < 5 or bc[:3] != b"\x1bLJ":
        return "not-luajit-bytecode", None
    ver = bc[3]
    flags, i = uleb(bc, 4)
    if ver == 1:
        kind = "LuaJIT 2.0"
    elif ver == 2:
        kind = "LuaJIT 2.1 " + ("GC64" if flags & 0x08 else "non-GC64")
    else:
        kind = f"unknown bytecode v{ver}"
    chunk = None
    if not flags & 0x02:  # not stripped: chunk name follows
        n, i = uleb(bc, i)
        chunk = bc[i:i + n].decode("utf-8", "replace")
    return kind, chunk


class Scanner:
    def __init__(self):
        self.funcs = []
        self.counts = {}

    def fmt_key(self, k):
        if isinstance(k, str):
            return "." + k if k.isidentifier() else f"[{k!r}]"
        if isinstance(k, float) and k.is_integer():
            return f"[{int(k)}]"
        return f"[{k!r}]"

    def value(self, b, path):
        t = b.u8()
        self.counts[t] = self.counts.get(t, 0) + 1
        if t == T_NIL:
            return None
        if t == T_BOOL:
            return bool(b.u8())
        if t == T_NUM:
            return struct.unpack("<d", b.take(8))[0]
        if t == T_STR:
            return b.take(b.u32()).decode("utf-8", "replace")
        if t in (T_TAB, T_USR):
            tag = b.u8()
            if tag == TREF:
                b.i32()
                return "<ref>"
            if tag == TVAL and t == T_USR:
                return "<userdata>"
            body = Blob(b.take(b.u32()))
            if tag == TUSR:  # __persist: body is {1 = callback function}
                self.table(body, path + "<__persist>")
                return "<persisted>"
            self.table(body, path)
            return "<table>"
        if t == T_FUN:
            tag = b.u8()
            if tag == TREF:
                b.i32()
                return "<fnref>"
            bc = b.take(b.u32())
            kind, chunk = bytecode_info(bc)
            self.funcs.append((path or "<root>", kind, chunk, len(bc)))
            self.table(Blob(b.take(b.u32())), path + "<upvalues>")
            return "<function>"
        raise ValueError(f"unknown type byte {t} at offset {b.p - 1} under {path or '<root>'}")

    def table(self, b, path):
        while b.p < len(b.d):
            k = self.value(b, path + "<key>")
            self.value(b, path + self.fmt_key(k))


def scan(fn):
    data = open(fn, "rb").read()
    if not data or data[0] != MAGIC:
        print(f"{fn}: not an lmarshal blob (first byte {data[:1].hex() or 'empty'})")
        return None
    s = Scanner()
    b = Blob(data)
    b.p = 1
    s.value(b, "")
    names = {T_NIL: "nil", T_BOOL: "bool", T_NUM: "number", T_STR: "string",
             T_TAB: "table", T_FUN: "function", T_USR: "userdata"}
    summary = ", ".join(f"{names.get(k, k)}={v}" for k, v in sorted(s.counts.items()))
    print(f"{fn}: {len(data)} bytes, {summary}")
    for path, kind, chunk, size in s.funcs:
        print(f"  FUNCTION {path}  [{kind}, {size} B bytecode, chunk {chunk!r}]")
    return s


if __name__ == "__main__":
    found = False
    for f in sys.argv[1:]:
        try:
            s = scan(f)
            found |= bool(s and s.funcs)
        except ValueError as e:
            print(f"{f}: PARSE ERROR {e}")
            found = True
    sys.exit(2 if found else 0)
