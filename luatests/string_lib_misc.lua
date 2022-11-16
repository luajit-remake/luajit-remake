-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

do --- __index metamethod is string library
  print(debug.getmetatable("").__index == string)
end

do --- string_op
  local t, y = {}, {}
  for i=1,100 do t[i] = string.char(i, 16+i, 32+i) end
  for i=1,100 do t[i] = string.reverse(t[i]) end
  print(t[100])
  for i=1,100 do t[i] = string.reverse(t[i]) end
  for i=1,100 do print(t[i]) end
  for i=1,100 do y[i] = string.upper(t[i]) end
  print(y[65])
  print(y[97])
  print(y[100])
  for i=1,100 do y[i] = string.lower(t[i]) end
  print(y[65])
  print(y[97])
  print(y[100])
end

