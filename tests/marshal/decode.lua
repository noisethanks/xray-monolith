-- decode.lua <dir> <case> : prints PASS, MISMATCH or an error for one case.
dofile("canon.lua")
local dir, name = arg[1], arg[2]
local blob = assert(io.open(dir .. "/" .. name .. ".bin", "rb")):read("*a")
local want = assert(io.open(dir .. "/" .. name .. ".canon", "rb")):read("*a")
local ok, v = pcall(marshal.decode, blob)
if not ok then print("DECODE_ERROR " .. tostring(v)) return end
local got = canon(v)
if got == want then print("PASS") else
  local i = 1 while got:sub(i, i) == want:sub(i, i) do i = i + 1 end
  print(string.format("MISMATCH at byte %d: want ...%s... got ...%s...", i, want:sub(i, i+60), got:sub(i, i+60)))
end
