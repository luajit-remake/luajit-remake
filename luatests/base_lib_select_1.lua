-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

do --- select #
-- Test whether select("#", 3, 4) returns the correct number of arguments. 
  local x = 0
  for i=1,100 do
    x = x + select("#", 3, 4)
  end
  print(x)
end

do --- select modf
-- Test whether select("#", func()) also works with func returning multiple values
  local x = 0
  math.frexp(3)
  for i=1,100 do
    x = x + select("#", math.modf(i))
  end
  print(x)
end

do --- select 1 
  local x = 0
  for i=1,100 do
    x = x + select(1, i)
  end
  print(x)
end

do --- select 2
  local x, y = 0, 0
  for i=1,100 do
    local a, b = select(2, 1, i, i+10)
    x = x + a
    y = y + b
  end
  print(x,y)
end

do --- select vararg #
  local function f(a, ...)
    local x = 0
    for i=1,select('#', ...) do
      x = x + select(i, ...)
    end
    print(x)
  end
  for i=1,1 do
    f(1, 1)
    f(3, 1, 2)
    f(15, 1, 2, 3, 4, 5)
    f(0)
    f(3200, 
1,2,3,4,5,6,7,8,9,10,
11,12,13,14,15,16,17,18,19,20,
21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,
51,52,53,54,55,56,57,58,59,60,
61,62,63,64,65,66,67,68,69,70,
71,72,73,74,75,76,77,78,79,80,
81,82,83,84,85,86,87,88,89,90,
91,92,93,94,95,96,97,98,99,100)
  end
end

do --- select vararg i
  local function f(a, ...)
    local x = 0
    for i=1,20 do
      local b = select(i, ...)
      if b then x = x + b else x = x + 9 end
    end
    print(x)
  end
  for i=1,1 do
    f(172, 1)
    f(165, 1, 2)
    f(150, 1, 2, 3, 4, 5)
    f(180)
    f(640, 
1,2,3,4,5,6,7,8,9,10,
11,12,13,14,15,16,17,18,19,20,
21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,
51,52,53,54,55,56,57,58,59,60,
61,62,63,64,65,66,67,68,69,70,
71,72,73,74,75,76,77,78,79,80,
81,82,83,84,85,86,87,88,89,90,
91,92,93,94,95,96,97,98,99,100)
  end
end

do --- select vararg 4
  local function f(a, ...)
    local x = 0
    for i=1,20 do
      local b = select(4, ...)
      if b then x = x + b else x = x + 9 end
    end
    print(x)
  end
  for i=1,1 do
    f(180, 1)
    f(180, 1, 2)
    f(80, 1, 2, 3, 4, 5)
    f(180)
    f(640, 
1,2,3,4,5,6,7,8,9,10,
11,12,13,14,15,16,17,18,19,20,
21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,
41,42,43,44,45,46,47,48,49,50,
51,52,53,54,55,56,57,58,59,60,
61,62,63,64,65,66,67,68,69,70,
71,72,73,74,75,76,77,78,79,80,
81,82,83,84,85,86,87,88,89,90,
91,92,93,94,95,96,97,98,99,100)
  end
end

do --- varg-select specialisation requires guard against select
  local select = select
  local function f(...)
    for i = 1, 100 do
      print(select('#', ...))
      if i == 75 then
        select = function() return "x" end
      end
    end
  end
  f(1)
end
