-- test the cacheable dictionary case

function addProps1(t)
	for i=1,5 do
		t["a" .. i ] = i
	end
	return t
end

function addProps2(t)
	for i=6,10 do
		t["a" .. i ] = i
	end
	return t
end

function get0_impl(t, i)
	return t[i]
end

function get1_impl(t, i)
	return t[i]
end

function get2_impl(t, i)
	return t[i]
end

function get3_impl(t, i)
	return t[i]
end

function get4_impl(t, i)
	return t[i]
end

function get0(t)
	return get0_impl(t, 0)
end

function get1(t)
	return get1_impl(t, 1)
end

function get2(t)
	return get2_impl(t, 2)
end

function get3(t)
	return get3_impl(t, 'a3')
end

function get4(t)
	return get4_impl(t, 'a8')
end

print('-- test 1 --')
t1 = addProps1({})
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

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
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1 = addProps2(t1)

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'c'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 'd'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

print('-- test 2 --')
t1 = addProps1({ [1] = 56, [2] = 78 })
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

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
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1 = addProps2(t1)

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'c'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 'd'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

print('-- test 3 --')
t1 = addProps1({ [-1] = '2333', [0] = 456 })
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

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
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1 = addProps2(t1)

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'c'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 'd'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

print('-- test 4 --')
t1 = addProps1({ })

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
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[2] = 'a'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'b'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

setmetatable(t1, {})

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = nil
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 123
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1 = addProps2(t1)

print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[0] = 'c'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

t1[1] = 'd'
print(get0(t1))
print(get0(t1))
print(get1(t1))
print(get1(t1))
print(get2(t1))
print(get2(t1))
print(get3(t1))
print(get3(t1))
print(get4(t1))
print(get4(t1))

