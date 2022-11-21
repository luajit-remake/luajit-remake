-- Author: Mike Pall
-- https://github.com/LuaJIT/LuaJIT-test-cleanup
-- 
-- Modified by Haoran, see comment below
--
local function nsieve(p, m)
  -- This is a huge array (40M elements when N=12) 
  -- It is important to make the array continuous, otherwise under current LJR setting,
  -- non-continuous array will go into sparse map mode once it grows beyond 100k, and ruins performances
  -- so the loop must start at 1, not 2
  for i=1,m do p[i] = true end
  local count = 0
  for i=2,m do
    if p[i] then
      for k=i+i,m,i do p[k] = false end
      count = count + 1
    end
  end
  return count
end

local N = tonumber(arg and arg[1]) or 1
if N < 2 then N = 2 end
local primes = {}

for i=0,2 do
  local m = (2^(N-i))*10000
  io.write(string.format("Primes up to %8d %8d\n", m, nsieve(primes, m)))
end

