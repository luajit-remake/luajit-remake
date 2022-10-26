print('-- test 1 --')

function test1()
	return gb1
end

print(test1())
print(test1())
gb1 = 1
print(test1())
print(test1())
gb1 = "xx"
print(test1())
print(test1())
gb1 = nil
print(test1())
print(test1())

print('-- test 2 --')

function test2()
	return gb2
end

print(test2())
print(test2())
setmetatable(_G, {
	__index = function(base, idx) 
		print("in metamethod", idx)
		return 233
	end
})
print(test2())
print(test2())
setmetatable(_G, {
	__index = function(base, idx) 
		print("in metamethod2", idx)
		return 345
	end
})
print(test2())
print(test2())
gb2 = "yy"
print(test2())
print(test2())
gb2 = nil
print(test2())
print(test2())
gb2 = "aa"
print(test2())
print(test2())
gb2 = nil
print(test2())
print(test2())
setmetatable(_G, {})
print(test2())
print(test2())
gb2 = "zz"
print(test2())
print(test2())
gb2 = nil
setmetatable(_G, nil)
print(test2())
print(test2())

