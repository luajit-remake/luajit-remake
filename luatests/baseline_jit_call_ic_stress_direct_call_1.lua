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

function f0(g)
	local result = g()
	return result
end

function f1(g, x)
	local result = g(x)
	return result
end

function f2(g, x1, x2)
	local result = g(x1, x2)
	return result
end

function f5(g, x1, x2, x3, x4, x5)
	local result = g(x1, x2, x3, x4, x5)
	return result
end

function f12(g, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12)
	local result = g(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12)
	return result
end

function g0_1() return 1 end
function g0_2() return 2 end
function g0_3() return 3 end

print('---- 0 param no tail no va ----')
do
	choiceList = { g0_1, g0_2, g0_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1]
		print(f0(fn))
	end
end

function g1_1(x) return x + 1 end
function g1_2(x) return x + 2 end
function g1_3(x) return x + 3 end

print('---- 1 param no tail no va ----')
do
	choiceList = { g1_1, g1_2, g1_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1]
		print(f1(fn, rand() % 1000))
	end
end

function g2_1(x1, x2) return x1 + x2 + 1 end
function g2_2(x1, x2) return x1 + x2 + 2 end
function g2_3(x1, x2) return x1 + x2 + 3 end

print('---- 2 param no tail no va ----')
do
	choiceList = { g2_1, g2_2, g2_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1]
		print(f2(fn, rand() % 1000, rand() % 1000))
	end
end

function g5_1(x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + 1 end
function g5_2(x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + 2 end
function g5_3(x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + 3 end

print('---- 5 param no tail no va ----')
do
	choiceList = { g5_1, g5_2, g5_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1]
		print(f5(fn, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000))
	end
end

function g12_1(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + 1 end
function g12_2(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + 2 end
function g12_3(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + 3 end

print('---- 12 param no tail no va ----')
do
	choiceList = { g12_1, g12_2, g12_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1]
		print(f12(fn, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000))
	end
end

