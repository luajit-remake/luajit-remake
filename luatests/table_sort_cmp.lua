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

rand = create_rng(12345)

n = 2000
t = {}
for i = 1,n do
	t[i] = { x = rand() % 10000, y = rand() % 10000 }
end

function cmp(lhs, rhs)
	if lhs.x ~= rhs.x then
		return lhs.x > rhs.x
	else
		return lhs.y > rhs.y
	end
end
table.sort(t, cmp)

for i = 1,n-1 do
	assert(not cmp(t[i+1], t[i]))
end

