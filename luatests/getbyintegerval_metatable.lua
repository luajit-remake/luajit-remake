t = { name = "t" }

print('-- test 1 --')
print(t[1])
print(t[2])

print('-- test 2 --')
t[1] = "x"
setmetatable(t, {
	__index = function(a, b, c)
		print("f1", a.name, b, c)
		return 233
	end
})
print(t[1])
print(t[2])

print('-- test 3 --')
t[1] = nil
t[2] = "y"
print(t[1])
print(t[2])

print('-- test 4 --')
t[1] = false
t[2] = 1.2
print(t[1])
print(t[2])

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
print(t[1])
print(t[2])

print('-- test 6 --')
t[1] = nil
t[2] = 0
print((pcall(function() print(t[1]) end)))
print(t[2])

print('-- test 7 --')
debug.setmetatable(234, {
	__index = function(a,b,c,d)
		print("f3", a, b, c, d)
		return 433
	end
})
print(t[1])
print(t[2])

print('-- test 8 --')
debug.setmetatable(345, {})
print((pcall(function() print(t[1]) end)))
print(t[2])

print('-- test 9 --')
t = false
print((pcall(function() print(t[1]) end)))
print((pcall(function() print(t[2]) end)))

print('-- test 10 --')
debug.setmetatable(true, {
	__index = function(a,b,c)
		print("f3", a, b, c)
		return 455
	end
})
print(t[1])
print(t[2])

print('-- test 11 --')
debug.setmetatable(true, {
	__index = 566
})
print((pcall(function() print(t[1]) end)))
print((pcall(function() print(t[2]) end)))

print('-- test 12 --')
debug.setmetatable(677, {
	__call = function(a,b,c,d)
		print("f4", a, b, c, d)
		return 788
	end
})
print((pcall(function() print(t[1]) end)))
print((pcall(function() print(t[2]) end)))

print('-- test 13 --')
debug.setmetatable(677, {
	__index = function(a,b,c,d)
		print("f5", a, b, c, d)
		return 899
	end
})
print(t[1])
print(t[2])

print('-- test 14 --')
t1 = { name = "t1", [1] = 1 }
t2 = { name = "t2", [2] = 2 }
t3 = { name = "t3", [3] = 3 }
t4 = { name = "t4", [4] = 4 }
t5 = { name = "t5", [5] = 5 }
t6 = { name = "t6", [6] = 6 }
t7 = { name = "t7", [7] = 7 }
t8 = { name = "t8", [8] = 8 }
t9 = { name = "t9", [9] = 9 }
t10 = { name = "t10", [10] = 10 }
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

print(t1[1])
print(t1[2])
print(t1[3])
print(t1[4])
print(t1[5])
print(t1[6])
print(t1[7])
print(t1[8])
print(t1[9])
print(t1[10])
print(t1[11])

print(t2[1])
print(t2[2])
print(t2[3])
print(t2[11])

print('-- test 15 --')

setmetatable(t10, { __index = 456 })

print(t1[10])
print(t1[11])

print(t2[3])
print(t2[11])

print('-- test 16 --')

t11 = { name = "t11", [11] = 11 }

debug.setmetatable(-123, {
	__index = t11
})

print(t1[10])
print(t1[11])
print(t1[12])

print(t2[3])
print(t2[11])
print(t2[12])


