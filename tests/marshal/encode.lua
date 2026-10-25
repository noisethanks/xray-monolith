dofile("canon.lua") dofile("corpus.lua")
local dir = arg[1]
for _, name in ipairs(ORDER) do
  local v = CASES[name]()
  local blob = marshal.encode(v)
  local f = assert(io.open(dir .. "/" .. name .. ".bin", "wb")) f:write(blob) f:close()
  f = assert(io.open(dir .. "/" .. name .. ".canon", "wb")) f:write(canon(v)) f:close()
end
