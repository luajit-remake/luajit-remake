-- Copyright (c) 2017 Gabriel de Quadros Ligneul
-- MIT License
-- https://github.com/gligneul/Lua-Benchmarks
--
-- fixed-point operator
local Z = function (le)
      local a = function (f)
        return le(function (x) return f(f)(x) end)
      end
      return a(a)
    end


-- non-recursive factorial

local F = function (f)
      return function (n)
               if n == 0 then return 1
               else return n*f(n-1) end
             end
    end

local fat = Z(F)

local N = tonumber((arg and arg[1])) or 100

local s = 0
for iter = 1, N do
	for i = 1, 100 do s = s + fat(i) end
end
print(s)

