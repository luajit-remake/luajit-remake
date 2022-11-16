-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

local sub = string.sub

do --- smoke
  assert(sub("abc", 2) == "bc")
  assert(sub(123, "2") == "23")
end

do --- all bar substrings
  local subs = {
    {"b", "ba", "bar"},
    { "",  "a",  "ar"},
    { "",   "",   "r"}
  }
  for i = 1, 3 do
    for j = 1, 3 do
      assert(sub("bar", i, j) == subs[i][j])
      assert(sub("bar", -4+i, j) == subs[i][j])
      assert(sub("bar", i, -4+j) == subs[i][j])
      assert(sub("bar", -4+i, -4+j) == subs[i][j])
    end
  end
end

do --- jit sub 1 eq
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 1) == "a" then x = x + 1 end
  end
  assert(x == 100)
end

do --- jit sub 1 ne (contents)
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 1) == "b" then x = x + 1 end
  end
  assert(x == 0)
end

do --- jit sub 1 ne (rhs too long)
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 1) == "ab" then x = x + 1 end
  end
  assert(x == 0)
end

do --- jit sub 1,2 ne
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 2) == "a" then x = x + 1 end
  end
  assert(x == 0)
end

do --- jit sub 1,k eq
  local s = "abcde"
  local x = 0
  local k = 1
  for i=1,100 do
    if sub(s, 1, k) == "a" then x = x + 1 end
  end
  assert(x == 100)
end

do --- jit sub 1,k ne (contents)
  local s = "abcde"
  local x = 0
  local k = 1
  for i=1,100 do
    if sub(s, 1, k) == "b" then x = x + 1 end
  end
  assert(x == 0)
end

do --- jit sub 1,k ne (rhs too long)
  local s = "abcde"
  local x = 0
  local k = 1
  for i=1,100 do
    if sub(s, 1, k) == "ab" then x = x + 1 end
  end
  assert(x == 0)
end

do --- jit sub 1,2 eq
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 2) == "ab" then x = x + 1 end
  end
  assert(x == 100)
end

do --- jit sub 1,3 eq
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 3) == "abc" then x = x + 1 end
  end
  assert(x == 100)
end

do --- jit sub 1,4 eq
  local s = "abcde"
  local x = 0
  for i=1,100 do
    if sub(s, 1, 4) == "abcd" then x = x + 1 end
  end
  assert(x == 100)
end

