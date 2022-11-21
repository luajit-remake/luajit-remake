-- Copyright (c) 2017 Gabriel de Quadros Ligneul
-- MIT License
-- https://github.com/gligneul/Lua-Benchmarks
--
local floor = math.floor

function heapsort(n, ra)
    local j, i, rra
    local l = floor(n/2) + 1
    -- local l = (n//2) + 1
    local ir = n;
    while 1 do
        if l > 1 then
            l = l - 1
            rra = ra[l]
        else
            rra = ra[ir]
            ra[ir] = ra[1]
            ir = ir - 1
            if (ir == 1) then
                ra[1] = rra
                return
            end
        end
        i = l
        j = l * 2
        while j <= ir do
            if (j < ir) and (ra[j] < ra[j+1]) then
                j = j + 1
            end
            if rra < ra[j] then
                ra[i] = ra[j]
                i = j
                j = j + i
            else
                j = ir + 1
            end
        end
        ra[i] = rra
    end
end

function create_rng(seed)
  local Rm, Rj = {}, 1
  for i=1,17 do Rm[i] = 0 end
  for i=17,1,-1 do
    seed = (seed*9069) % (2^31)
    Rm[i] = seed
  end
  return function()
      local j, m = Rj, Rm
      local h = j - 5
      if h < 1 then h = h + 17 end
      local k = m[h] - m[j]
      if k < 0 then k = k + 2147483647 end
      m[j] = k
      if j < 17 then Rj = j + 1 else Rj = 1 end
      return k
  end
end 

random = create_rng(12345)

local Num = tonumber((arg and arg[1])) or 4
for i=1,Num do
  local N = tonumber((arg and arg[2])) or 10000
  local a = {}
  for i=1,N do a[i] = random() end
  heapsort(N, a)
  for i=1,N-1 do assert(a[i] <= a[i+1]) end
end

