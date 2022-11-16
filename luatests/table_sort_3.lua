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
			assert(not (a[i] > a[i+1]))
			assert(a[i].idx >= a[i+1].idx)
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
			assert(not (a[i] > a[i+1]))
			assert(a[i].idx >= a[i+1].idx)
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
			assert(not (a[i] > a[i+1]))
			assert(a[i].idx >= a[i+1].idx)
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

mt = {
	__lt = function(a,b)
		return a.idx > b.idx
	end
}

print('-- test 1 --')
local a, checker = test_continuous(2000, function() return setmetatable({ idx = math.random(1, 1000) }, mt) end)
table.sort(a)
checker()

print('-- test 2 --')
local a, checker = test_continuous(2000, function() return setmetatable({ idx = math.random(1, 10) }, mt) end)
table.sort(a)
checker()

print('-- test 3 --')
local a, checker = test_continuous(2000, function() return setmetatable({ idx = math.random() }, mt) end)
table.sort(a)
checker()

print('-- test 4 --')
local a, checker = test_not_continuous(2000, function() return setmetatable({ idx = math.random(1, 1000) }, mt) end)
table.sort(a)
checker()

print('-- test 5 --')
local a, checker = test_not_continuous(2000, function() return setmetatable({ idx = math.random(1, 10) }, mt) end)
table.sort(a)
checker()

print('-- test 6 --')
local a, checker = test_not_continuous(2000, function() return setmetatable({ idx = math.random() }, mt) end)
table.sort(a)
checker()

print('-- test 7 --')
local a, checker = test_not_continuous_2(2000, function() return setmetatable({ idx = math.random(1, 1000) }, mt) end)
table.sort(a)
checker()

print('-- test 8 --')
local a, checker = test_not_continuous_2(2000, function() return setmetatable({ idx = math.random(1, 10) }, mt) end)
table.sort(a)
checker()

print('-- test 9 --')
local a, checker = test_not_continuous_2(2000, function() return setmetatable({ idx = math.random() }, mt) end)
table.sort(a)
checker()


