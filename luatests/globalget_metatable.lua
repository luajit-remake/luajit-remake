print('-- test 1 --')
print(x)
print(y)

setmetatable(_G, {
	__index = function(a,b,c)
		print("f1",a == _G,b,c)
		return 123
	end
})

print('-- test 2 --')
print(x)
print(y)

print('-- test 3 --')
x = "xx"

print(x)
print(y)

print('-- test 4 --')
t1 = { y = "ww" }

setmetatable(_G, {
	__index = t1
})

print(x)
print(y)

print('-- test 5 --')
setmetatable(_G, {
	__index = 233
})

debug.setmetatable(344, {
	__index = {
		x = "x1",
		y = "y1"
	}
})
	
print(x)
print(y)

print('-- test 6 --')
x = nil
print(x)
print(y)

print('-- test 7 --')
x = "abc"
print(x)
print(y)

print('-- test 8 --')
debug.setmetatable(344, {
	__index = function(a,b,c,d)
		print("f2", a,b,c,d)
		return 233
	end
})

print(x)
print(y)

print('-- test 9 --')
debug.setmetatable(344, {})

print(x)
print((pcall(function() print(y) end)))

print('-- test 10 --')
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

setmetatable(_G, { __index = t1 })

print(x)
print(y)
print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)
print(a8)
print(a9)
print(a10)
print(a11)
print(a12)

