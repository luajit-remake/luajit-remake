-- Author: Mike Pall 
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- This file contains modifications. See above repo for the original version.

local create = coroutine.create
local wrap = coroutine.wrap
local resume = coroutine.resume
local yield = coroutine.yield

do --- cogen
  local function cogen(x)
    return wrap(function(n) repeat x = x+n; n = yield(x) until false end),
	   wrap(function(n) repeat x = x*n; n = yield(x) until false end)
  end

  local a,b=cogen(3)
  local c,d=cogen(5)
  print(d(b(c(a(d(b(c(a(1)))))))))
end

do --- cofunc +luajit
  verify_count = 0
  local function verify(what, expect, ...)
    verify_count = verify_count + 1
    local got = {...}
    for i=1,100 do
      if expect[i] ~= got[i] then
        print(verify_count, "FAIL " .. what)
      end
      if expect[i] == nil then
        break
      end
    end
  end

  local function cofunc(...)
    verify("call", { 1, "foo" }, ...)
    verify("yield", { "bar" }, yield(2, "test"))
    verify("pcall yield", { true, "again" }, pcall(yield, "from pcall"))
    return "end"
  end

  local co = create(cofunc)
  verify("resume", { true, 2, "test" }, resume(co, 1, "foo"))
  verify("resume pcall", { true, "from pcall" }, resume(co, "bar"))
  verify("resume end", { true, "end" }, resume(co, "again"))
  print('verify_count = ', verify_count)
end

do --- assorted +luajit
  verify_count = 0
  local function verify(expect, func, ...)
    verify_count = verify_count + 1
    local co = create(func)
    for i=1,100 do
      local ok, res = resume(co, ...)
      if not ok then
        if expect[i] ~= nil then
          print(verify_count, "too few results: ["..i.."] = ",expect[i]," (got: ",res,")")
        end
        break
      end
      if expect[i] ~= res then
        print(verify_count, "bad result: ["..i.."] = ",res," (should be: ",expect[i],")")
      end
    end
  end

  verify({ 42, 99 },
    function(x) pcall(yield, x) return 99 end,
    42)

  verify({ 42, 99 },
    function(x) pcall(function(y) yield(y) end, x) return 99 end,
    42)

  -- verify({ 42, 99 },
  --  function(x) xpcall(yield, debug.traceback, x) return 99 end,
  --  42)

  verify({ 45, 44, 43, 42, 99 },
    function(x, y)
      for i in
        function(o, k)
          yield(o+k)
          if k ~= 0 then return k-1 end
        end,x,y do
      end
      return 99
    end,
    42, 3)

  verify({ 84, 99 },
    function(x)
      local o = setmetatable({ x },
        {__add = function(a, b) yield(a[1]+b[1]) return 99 end })
      return o+o
    end,
    42)
    
  print('verify_count = ', verify_count)
end 
