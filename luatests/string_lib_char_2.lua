-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

local char = string.char

do --- jit one char
  local y
  for i=1,100 do y = char(65) end
  assert(y == "A")
  local x = 97
  for i=1,100 do y = char(x) end
  assert(y == "a")
  x = "98"
  for i=1,100 do y = char(x) end
  assert(y == "b")
  for i=1,100 do y = char(32+i) end
  print(string.byte(y))
end

do --- jit until out of bounds
  local y
  assert(not pcall(function()
    for i=1,200 do y = char(100+i) end
  end))
  print(string.byte(y))
end

do --- jit five chars
  local y
  for i=1,100 do y = char(65, 66, i, 67, 68) end
  print(y)
end
