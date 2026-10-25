-- Deterministic structural dump of a Lua value. Shared refs and cycles become #n back-references.
local function num(n)
  if n ~= n then return "nan" end
  if n == math.huge then return "inf" elseif n == -math.huge then return "-inf" end
  if n == 0 and 1/n < 0 then return "-0" end
  return string.format("%.17g", n)
end
local function key_sort(a, b)
  local ta, tb = type(a), type(b)
  if ta ~= tb then return ta < tb end
  if ta == "number" then return a < b end
  return tostring(a) < tostring(b)
end
function canon(v)
  local seen, n, out = {}, 0, {}
  local function w(x)
    local t = type(x)
    if t == "nil" then out[#out+1] = "nil"
    elseif t == "boolean" then out[#out+1] = tostring(x)
    elseif t == "number" then out[#out+1] = "n:" .. num(x)
    elseif t == "string" then out[#out+1] = string.format("s%d:%q", #x, x)
    elseif t == "table" or t == "function" then
      if seen[x] then out[#out+1] = "#" .. seen[x]; return end
      n = n + 1; seen[x] = n
      if t == "function" then
        -- Identity of a function = its upvalues plus its behaviour on fixed probes.
        out[#out+1] = "f{"
        local i = 1
        while true do
          local name, uv = debug.getupvalue(x, i)
          if not name then break end
          out[#out+1] = "u" .. i .. "="; w(uv); out[#out+1] = ","
          i = i + 1
        end
        local ok, r = pcall(x, 7, "probe")
        out[#out+1] = "call=" .. tostring(ok) .. ":"; w(ok and r or "err"); out[#out+1] = "}"
      else
        local keys = {}
        for k in pairs(x) do keys[#keys+1] = k end
        table.sort(keys, key_sort)
        out[#out+1] = "{"
        for _, k in ipairs(keys) do w(k); out[#out+1] = "="; w(x[k]); out[#out+1] = ";" end
        out[#out+1] = "}"
      end
    else out[#out+1] = "<" .. t .. ">" end
  end
  w(v)
  return table.concat(out)
end
