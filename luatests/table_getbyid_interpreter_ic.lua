function create1(x1, y1)
	return { 
		x = x1,
		y = y1
	}
end

function test1(o)
	return o.x
end

function hide(tmp) return tmp end

function create2(x1, y1)
	local tmp = {}
	tmp = hide(tmp)
	tmp.x = x1
	tmp.y = y1
	return tmp
end

function test2(o)
	return o.x
end

function create3(x1, y1)
	local tmp = { 
		x = x1,
		y = y1
	}
	setmetatable(tmp, {
		__index = function(base, idx)
			print("in metatmethod 3", idx)
			return 2333
		end
	})
	return tmp
end

function test3(o)
	return o.x
end

function create4(x1, y1)
	local tmp = {}
	tmp = hide(tmp)
	tmp.xx = x1
	tmp.yy = y1
	setmetatable(tmp, {
		__index = function(base, idx)
			print("in metatmethod 4", idx)
			return 2333
		end
	})
	return tmp
end

function test4(o)
	return o.xx
end

function create5(x1, y1)
	local tmp = {}
	tmp = hide(tmp)
	tmp.x = x1
	tmp.y = y1
	for i=1,500 do
		local key = "a" .. i
		tmp[key] = 123
	end
	return tmp
end

function test5(o)
	return o.x
end

function create6(x1, y1)
	local tmp = {}
	tmp = hide(tmp)
	setmetatable(tmp, {
		__index = function(base, idx)
			print("in metatmethod 6", idx)
			return 2333
		end
	})
	tmp.xxx = x1
	tmp.yyy = y1
	for i=1,500 do
		local key = "a" .. i
		tmp[key] = 123
	end
	return tmp
end

function test6(o)
	return o.xxx
end

function dotestImpl(createFn, testFn, name, v1, v2, v3, v4)
	o1 = createFn(v1, v2)
	o2 = createFn(v3, v4)
	
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))
	
	o1[name] = 5
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))

	o1[name] = nil
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))

	o1[name] = 6
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))
	
	setmetatable(o1, nil)
	setmetatable(o2, nil)
	
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))
	
	o1[name] = 5
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))

	o1[name] = nil
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))

	o1[name] = 6
	print(testFn(o1))
	print(testFn(o1))
	print(testFn(o2))
	print(testFn(o2))
	print(testFn(o1))
	print(testFn(o1))
end

function dotest(createFn, testFn, propName)
	print('-- case 1 --')
	dotestImpl(createFn, testFn, propName, 1, 2, 3, 4)
	print('-- case 2 --')
	dotestImpl(createFn, testFn, propName, nil, 10, 11, 12)
end

print('-- test create1 --')
dotest(create1, test1, "x")

print('-- test create2 --')
dotest(create2, test2, "x")

print('-- test create3 --')
dotest(create3, test3, "x")

print('-- test create4 --')
dotest(create4, test4, "xx")

print('-- test create5 --')
dotest(create5, test5, "x")

print('-- test create6 --')
dotest(create6, test6, "xxx")

