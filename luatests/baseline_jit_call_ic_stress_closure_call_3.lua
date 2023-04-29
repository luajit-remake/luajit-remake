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

function f0(g, ...)
	local result = g(...)
	return result
end

function f1(g, ...)
	local result = g(...)
	return result
end

function f2(g, x1, ...)
	local result = g(x1, ...)
	return result
end

function f5(g, x1, x2, x3, ...)
	local result = g(x1, x2, x3, ...)
	return result
end

function f12(g, x1, x2, x3, x4, x5, x6, ...)
	local result = g(x1, x2, x3, x4, x5, x6, ...)
	return result
end

function gen_g0_1(k1)
	return function () return k1 + 1 end
end
function gen_g0_2(k1) 
	return function() return k1 + 2 end
end
function gen_g0_3(k1) 
	return function() return k1 + 3 end
end

print('---- 0 param no tail va ----')
do
	choiceList = { gen_g0_1, gen_g0_2, gen_g0_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1](rand() % 1000)
		print(f0(fn))
	end
end

function gen_g1_1(k1)
	return function(x) return x + k1 + 1 end
end
function gen_g1_2(k1)
	return function(x) return x + k1 + 2 end
end
function gen_g1_3(k1)
	return function(x) return x + k1 + 3 end
end

print('---- 1 param no tail va ----')
do
	choiceList = { gen_g1_1, gen_g1_2, gen_g1_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1](rand() % 1000)
		print(f1(fn, rand() % 1000))
	end
end

function gen_g2_1(k1)
	return function (x1, x2) return x1 + x2 + k1 +1 end
end
function gen_g2_2(k1)
	return function (x1, x2) return x1 + x2 + k1 + 2 end
end
function gen_g2_3(k1)
	return function (x1, x2) return x1 + x2 + k1 + 3 end
end

print('---- 2 param no tail va ----')
do
	choiceList = { gen_g2_1, gen_g2_2, gen_g2_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1](rand() % 1000)
		print(f2(fn, rand() % 1000, rand() % 1000))
	end
end

function gen_g5_1(k1)
	return function (x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + k1 + 1 end
end
function gen_g5_2(k1)
	return function (x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + k1 + 2 end
end
function gen_g5_3(k1)
	return function (x1, x2, x3, x4, x5) return x1 + x2 + x3 + x4 + x5 + k1 + 3 end
end

print('---- 5 param no tail va ----')
do
	choiceList = { gen_g5_1, gen_g5_2, gen_g5_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1](rand() % 1000)
		print(f5(fn, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000))
	end
end

function gen_g12_1(k1)
	return function (x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + k1 + 1 end
end
function gen_g12_2(k1)
	return function (x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + k1 + 2 end
end
function gen_g12_3(k1)
	return function (x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) return x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + x9 + x10 + x11 + x12 + k1 + 3 end
end

print('---- 12 param no tail va ----')
do
	choiceList = { gen_g12_1, gen_g12_2, gen_g12_3 }
	for i = 1, 30 do
		local fn = choiceList[rand() % 3 + 1](rand() % 1000)
		print(f12(fn, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000, rand() % 1000))
	end
end

