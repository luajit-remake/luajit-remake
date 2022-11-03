-- test the cacheable dictionary case

function makeCacheableDict(t)
	for i=1,1000 do
		t["a" .. i ] = i
	end
	return t
end

function get0(t)
	return t[0]
end

function get1(t)
	return t[1]
end

function get2(t)
	return t[2]
end

print('-- test 1 --')
t1 = makeCacheableDict({})
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

setmetatable(t1, {
	__index = function(base, idx)
		print('in metatable (test 1), idx = ', idx)
		return 2333
	end
})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

print('-- test 2 --')
t1 = makeCacheableDict({ [1] = 56, [2] = 78 })
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

setmetatable(t1, {
	__index = function(base, idx)
		print('in metatable (test 2), idx = ', idx)
		return 2333
	end
})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

print('-- test 3 --')
t1 = makeCacheableDict({ [-1] = '2333', [0] = 456 })
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

setmetatable(t1, {
	__index = function(base, idx)
		print('in metatable (test 3), idx = ', idx)
		return 2333
	end
})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

print('-- test 4 --')
t1 = makeCacheableDict({ })

setmetatable(t1, {
	__index = function(base, idx)
		print('in metatable (test 4, init mt), idx = ', idx)
		return 2333
	end
})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

setmetatable(t1, {})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
