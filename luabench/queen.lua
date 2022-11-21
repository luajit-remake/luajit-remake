-- Copyright (c) 2017 Gabriel de Quadros Ligneul
-- MIT License
-- https://github.com/gligneul/Lua-Benchmarks
--
local N = tonumber(arg and arg[1]) or 8    -- board size

solution = 0

-- check whether position (n,c) is free from attacks
local function isplaceok (a, n, c)
  for i = 1, n - 1 do   -- for each queen already placed
    if (a[i] == c) or                -- same column?
       (a[i] - i == c - n) or        -- same diagonal?
       (a[i] + i == c + n) then      -- same diagonal?
      return false            -- place can be attacked
    end
  end
  return true    -- no attacks; place is OK
end


-- print a board
local function printsolution (a)
  for i = 1, N do
    for j = 1, N do
      io.write(a[i] == j and "X" or "-", " ")
    end
    io.write("\n")
  end
  io.write("\n")
end


-- add to board 'a' all queens from 'n' to 'N'
local function addqueen (a, n)
  if n > N then    -- all queens have been placed?
    solution = solution + 1
  else  -- try to place n-th queen
    for c = 1, N do
      if isplaceok(a, n, c) then
        a[n] = c    -- place n-th queen at column 'c'
        addqueen(a, n + 1)
      end
    end
  end
end


-- run the program
addqueen({}, 1)

print(solution)

