-- Each case returns a fresh value. Functions are deliberately separated so a bytecode failure is attributable.
local C = {}
C.scalars = function() return {
  0, 1, -1, 2^31-1, 2^31, -2^31, 2^32, 2^52, 2^53, 2^53+1, 2^63, -2^63, 1e15+0.5,
  0.1, 1/3, 1e308, 5e-324, 2.2250738585072014e-308, -0.0, 0/0, math.huge, -math.huge,
  true, false } end
C.strings = function()
  local big = {} for i = 1, 70000 do big[i] = string.char(i % 256) end
  return { "", "a", "\0", "a\0b\0", string.rep("x", 255), string.rep("y", 65537),
           table.concat(big), "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", "\255\254" } end
C.keys = function()
  local t = {}
  t[1]=1 t[2]=2 t[4]=4 t[1000000]="sparse" t[-1]="neg" t[0]="zero" t[0.5]="half"
  t[2^40]="big" t[true]="T" t[false]="F" t["1"]="string one" t[{"tk"}]="table key"
  return t end
C.nested = function()
  local root = {} local cur = root
  for i = 1, 200 do cur.next = { depth = i, data = { i, i*2, tostring(i) } } cur = cur.next end
  return root end
C.shared_and_cycles = function()
  local a = { name = "a" } local b = { name = "b", a = a } a.b = b a.self = a
  local s = { "shared" }
  return { a = a, b = b, s1 = s, s2 = s, list = { s, s, a } } end
C.large_realistic = function()
  -- Shape loosely modelled on pstor style per-id records.
  local t = {}
  for id = 1, 20000 do
    t[id] = { id = id, sec = "stalker_" .. id, rank = id * 3.25, alive = id % 7 ~= 0,
              inv = { ["wpn_ak74"] = id % 3, ["ammo_5.45x39"] = 120 }, ts = 1729800000 + id }
  end
  t.meta = { version = 3, name = "corpus" }
  return t end
C.fn_plain = function() return { f = function(x) return (x or 0) * 2 end } end
C.fn_upvalues = function()
  local count, label, tbl = 41, "ctr", { 1, 2, 3 }
  return { f = function(x) count = count + 1 return label .. count .. ":" .. #tbl + (x or 0) end } end
C.fn_shared_upvalue = function()
  local shared = { n = 0 }
  local inc = function() shared.n = shared.n + 1 return shared.n end
  local get = function() return shared.n end
  return { inc = inc, get = get, shared = shared } end
C.fn_nested_proto = function()
  return { f = function(x) local g = function(y) return y + 1 end return g(x or 0) * 10 end } end
C.fn_many_consts = function()
  return { f = function(x) local t = { 1.5, 2^40, "k1", "k2", -0.25, 1e300 } return #t + (x or 0) end } end
C.fn_global_ref = function() return { f = function(x) return type(string) .. tostring(x) end } end
CASES = C
ORDER = { "scalars", "strings", "keys", "nested", "shared_and_cycles", "large_realistic",
  "fn_plain", "fn_upvalues", "fn_shared_upvalue", "fn_nested_proto", "fn_many_consts", "fn_global_ref" }
