t = { name = "t" }

print('-- test 1 --')
print(t.a)
print(t.b)

print('-- test 2 --')
t.a = "x"
setmetatable(t, {
	__index = function(a, b, c)
		print("f1", a.name, b, c)
		return 233
	end
})
print(t.a)
print(t.b)

print('-- test 3 --')
t.a = nil
t.b = "y"
print(t.a)
print(t.b)

print('-- test 4 --')
t.a = false
t.b = 1.2
print(t.a)
print(t.b)

print('-- test 5 --')
setmetatable(t, {
	__index = 123
})
debug.setmetatable(234, {
	__call = function(a,b,c,d)
		print("f2", a, b, c, d)
		return 344
	end
})
print(t.a)
print(t.b)

print('-- test 6 --')
t.a = nil
t.b = 0
print((pcall(function() print(t.a) end)))
print(t.b)

print('-- test 7 --')
debug.setmetatable(234, {
	__index = function(a,b,c,d)
		print("f3", a, b, c, d)
		return 433
	end
})
print(t.a)
print(t.b)

print('-- test 8 --')
debug.setmetatable(345, {})
print((pcall(function() print(t.a) end)))
print(t.b)

print('-- test 9 --')
t = false
print((pcall(function() print(t.a) end)))
print((pcall(function() print(t.b) end)))

print('-- test 10 --')
debug.setmetatable(true, {
	__index = function(a,b,c)
		print("f3", a, b, c)
		return 455
	end
})
print(t.a)
print(t.b)

print('-- test 11 --')
debug.setmetatable(true, {
	__index = 566
})
print((pcall(function() print(t.a) end)))
print((pcall(function() print(t.b) end)))

print('-- test 12 --')
debug.setmetatable(677, {
	__call = function(a,b,c,d)
		print("f4", a, b, c, d)
		return 788
	end
})
print((pcall(function() print(t.a) end)))
print((pcall(function() print(t.b) end)))

print('-- test 13 --')
debug.setmetatable(677, {
	__index = function(a,b,c,d)
		print("f5", a, b, c, d)
		return 899
	end
})
print(t.a)
print(t.b)

print('-- test 14 --')
t1 = { name = "t1", a1 = 1 }
t2 = { name = "t2", a2 = 2 }
t3 = { name = "t3", a3 = 3 }
t4 = { name = "t4", a4 = 4 }
t5 = { name = "t5", a5 = 5 }
t6 = { name = "t6", a6 = 6 }
t7 = { name = "t7", a7 = 7 }
t8 = { name = "t8", a8 = 8 }
t9 = { name = "t9", a9 = 9 }
t10 = { name = "t10", a10 = 10 }
setmetatable(t1, { __index = t2 })
setmetatable(t2, { __index = t3 })
setmetatable(t3, { __index = t4 })
setmetatable(t4, { __index = t5 })
setmetatable(t5, { __index = t6 })
setmetatable(t6, { __index = t7 })
setmetatable(t7, { __index = t8 })
setmetatable(t8, { __index = t9 })
setmetatable(t9, { __index = t10 })
setmetatable(t10, { __index = function(a,b,c)
	print("f6", a.name, b ,c)
	return 900
end })

print(t1.a1)
print(t1.a2)
print(t1.a3)
print(t1.a4)
print(t1.a5)
print(t1.a6)
print(t1.a7)
print(t1.a8)
print(t1.a9)
print(t1.a10)
print(t1.a11)

print(t2.a1)
print(t2.a2)
print(t2.a3)
print(t2.a11)

print('-- test 15 --')

setmetatable(t10, { __index = 456 })

print(t1.a10)
print(t1.a11)

print(t2.a3)
print(t2.a11)

print('-- test 16 --')

t11 = { name = "t11", a11 = 11 }

debug.setmetatable(-123, {
	__index = t11
})

print(t1.a10)
print(t1.a11)
print(t1.a12)

print(t2.a3)
print(t2.a11)
print(t2.a12)


