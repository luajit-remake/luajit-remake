function test_continuous(n, gen)
	local a = {}
	local cnt = {}
	for i = 1, n do
		a[i] = gen()
		assert(a[i] ~= nil)
		if cnt[a[i]] == nil then
			cnt[a[i]] = 0
		end
		cnt[a[i]] = cnt[a[i]] + 1
	end
	return a, function()
		for i = 1, n-1 do
			assert(a[i] >= a[i+1])
		end
		for i = 1, n do
			assert(cnt[a[i]] ~= nil)
			cnt[a[i]] = cnt[a[i]] - 1
		end
		for k,v in pairs(cnt) do
			assert(v == 0)
		end
		print('check ok')
	end
end

function test_not_continuous(n, gen)
	local a = {}
	local cnt = {}
	for i = n, 1, -1 do
		a[i] = gen()
		assert(a[i] ~= nil)
		if cnt[a[i]] == nil then
			cnt[a[i]] = 0
		end
		cnt[a[i]] = cnt[a[i]] + 1
	end
	return a, function()
		for i = 1, n-1 do
			assert(a[i] >= a[i+1])
		end
		for i = 1, n do
			assert(cnt[a[i]] ~= nil)
			cnt[a[i]] = cnt[a[i]] - 1
		end
		for k,v in pairs(cnt) do
			assert(v == 0)
		end
		print('check ok')
	end
end

function test_not_continuous_2(n, gen)
	local a = {}
	local cnt = {}
	local k = math.floor(n/40)
	for i = 1, k do
		a[i] = gen()
		assert(a[i] ~= nil)
		if cnt[a[i]] == nil then
			cnt[a[i]] = 0
		end
		cnt[a[i]] = cnt[a[i]] + 1
	end
	for i = n, k + 1, -1 do
		a[i] = gen()
		assert(a[i] ~= nil)
		if cnt[a[i]] == nil then
			cnt[a[i]] = 0
		end
		cnt[a[i]] = cnt[a[i]] + 1
	end
	return a, function()
		for i = 1, n-1 do
			assert(a[i] >= a[i+1])
		end
		for i = 1, n do
			assert(cnt[a[i]] ~= nil)
			cnt[a[i]] = cnt[a[i]] - 1
		end
		for k,v in pairs(cnt) do
			assert(v == 0)
		end
		print('check ok')
	end
end


print('-- test 1 --')
local a, checker = test_continuous(2000, function() return math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 2 --')
local a, checker = test_continuous(2000, function() return math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 3 --')
local a, checker = test_continuous(2000, function() return math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 4 --')
local a, checker = test_not_continuous(2000, function() return math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 5 --')
local a, checker = test_not_continuous(2000, function() return math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 6 --')
local a, checker = test_not_continuous(2000, function() return math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 7 --')
local a, checker = test_continuous(2000, function() return "a"..math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 8 --')
local a, checker = test_continuous(2000, function() return "a"..math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 9 --')
local a, checker = test_continuous(2000, function() return "a"..math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 10 --')
local a, checker = test_not_continuous(2000, function() return "a"..math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 11 --')
local a, checker = test_not_continuous(2000, function() return "a"..math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 12 --')
local a, checker = test_not_continuous(2000, function() return "a"..math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 13 --')
local a, checker = test_not_continuous_2(2000, function() return math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 14 --')
local a, checker = test_not_continuous_2(2000, function() return math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 15 --')
local a, checker = test_not_continuous_2(2000, function() return math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 16 --')
local a, checker = test_not_continuous_2(2000, function() return "a"..math.random(1, 1000) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 17 --')
local a, checker = test_not_continuous_2(2000, function() return "a"..math.random(1, 10) end)
table.sort(a, function(x,y) return x > y end)
checker()

print('-- test 18 --')
local a, checker = test_not_continuous_2(2000, function() return "a"..math.random() end)
table.sort(a, function(x,y) return x > y end)
checker()


