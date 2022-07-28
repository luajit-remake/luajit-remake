t1 = { name = "t1" }
t2 = { name = "t2" }

f1 = function(a,b,c)
	print("f1", a.name, b.name, c)
	return {}
end

setmetatable(t1, {
	__eq = f1
})

setmetatable(t2, {
	__eq = f1
})

print('--- test 1 ---')
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

f2 = function(a,b,c)
	print("f2", a.name, b.name, c)
	return
end

setmetatable(t1, {
	__eq = f2
})

setmetatable(t2, {
	__eq = f2
})

print('--- test 2 ---')
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 3 ---')

t3 = { name = "t3" }
t4 = { name = "t4" }

setmetatable(t1, {
	__eq = t3
})

setmetatable(t2, {
	__eq = t4
})


f3 = function(a,b,c,d)
	print("f3", a.name,b.name,c.name,d)
	return 12
end

setmetatable(t3, {
	__eq = f3,
	__call = function(a,b,c,d)
		print("t3_0", a.name,b.name,c.name,d)
		return 321
	end
})

setmetatable(t4, {
	__eq = f3
})
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 4 ---')

setmetatable(t3, {
	__call = function(a,b,c,d)
		print("t3_1", a.name,b.name,c.name,d)
		return 321
	end
})

setmetatable(t1, {
	__eq = t3
})

setmetatable(t2, {
	__eq = t3
})
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 5 ---')

setmetatable(t3, {
	__call = function(a,b,c,d)
		print("t3_2", a.name,b.name,c.name,d)
		return
	end
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 6 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn", a,b.name,c.name,d)
		return "abc"
	end
})

setmetatable(t1, {
	__eq = 1234
})

setmetatable(t2, {
	__eq = 1234
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 7 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_2", a,b.name,c.name,d)
		return false
	end
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 8 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_3", a,b.name,c.name,d)
		return "abc"
	end
})

setmetatable(t1, {
	__eq = 1234
})

setmetatable(t2, {
	__eq = "1234"
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 9 ---')

setmetatable(t1, {
	__eq = 0.0/0
})

setmetatable(t2, {
	__eq = 0.0/0
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)


print('--- test 10 ---')

setmetatable(t1, {
	__eq = 1 * 0
})

setmetatable(t2, {
	__eq = -1 * 0
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 11 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_4", a,b.name,c.name,d)
		return 
	end
})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)

print('--- test 12 ---')

debug.setmetatable(123, {})

print(t1 == t1)
print(t1 ~= t1)
print((pcall(function() print("bad", t1 == t2) end)))
print((pcall(function() print("bad", t1 ~= t2) end)))

print('test end')
 
