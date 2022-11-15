-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

do --- tonumber int
  local x = 0
  for i=1,100 do x = x + tonumber(i) end
  print(x)
end

do --- tonumber float
  local x = 0
  for i=1.5,100.5 do x = x + tonumber(i) end
  print(x)
end

do --- tostring int / tonumber
  local t = {}
  for i=1,100 do t[i] = tostring(i) end
  local x = 0
  for i=1,100 do assert(type(t[i]) == "string"); x = x + tonumber(t[i]) end
  print(x)
end

do --- tostring float / tonumber
  local t = {}
  for i=1,100 do t[i] = tostring(i+0.5) end
  local x = 0
  for i=1,100 do assert(type(t[i]) == "string"); x = x + tonumber(t[i]) end
  print(x)
end

do --- tonumber table
  for i=1,100 do assert(tonumber({}) == nil) end
end

do --- tostring int / tostring
  local t = {}
  for i=1,100 do t[i] = tostring(i) end
  for i=1,100 do t[i] = tostring(t[i]) end
  local x = 0
  for i=1,100 do assert(type(t[i]) == "string"); x = x + t[i] end
  print(x)
end

do --- tostring table __tostring
  local mt = { __tostring = function(t) return tostring(t[1]) end }
  local t = {}
  for i=1,100 do t[i] = setmetatable({i}, mt) end
  for i=1,100 do t[i] = tostring(t[i]) end
  local x = 0
  for i=1,100 do assert(type(t[i]) == "string"); x = x + t[i] end
  print(x)
end

do --- tostring table __tostring __call
  local r = setmetatable({},
			 { __call = function(x, t) return tostring(t[1]) end })
  local mt = { __tostring = r }
  local t = {}
  for i=1,100 do t[i] = setmetatable({i}, mt) end
  for i=1,100 do t[i] = tostring(t[i]) end
  local x = 0
  for i=1,100 do assert(type(t[i]) == "string"); x = x + t[i] end
  print(x)
end

do --- __tostring must be callable
  local t = setmetatable({}, { __tostring = "" })
  assert(pcall(function() tostring(t) end) == false)
end

print('test end')

