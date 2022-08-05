test = function(t, i)
	return t[i]
end

t = { name = "t" }

f0 = "a"
f1 = "b"
f2 = 1.2
f3 = 3
f4 = -5
f5 = false
f6 = 0.0/0
f7 = nil

print('-- test 1 --')
print(test(t, f0))
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 2 --')
t.a = "x"
t[3] = 8765
setmetatable(t, {
	__index = function(a, b, c)
		print("f1", a.name, b, c)
		return 233
	end
})
print(test(t, f0))
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 3 --')
t.a = nil
t.b = "y"
t[3] = nil
t[1.2] = 345
print(test(t, f0))
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 4 --')
t.a = false
t.b = 1.2
t[1.2] = nil
t[1.3] = 55
t[-5] = "www"
print(test(t, f0))
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


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
print(test(t, f0))
print(test(t, f1))
print((pcall(function() print(test(t, f2)) end)))
print((pcall(function() print(test(t, f3)) end)))
print(test(t, f4))
print((pcall(function() print(test(t, f5)) end)))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 6 --')
t.a = nil
t.b = 0
t[-5] = nil
t[3] = 22
t[false] = 44
print((pcall(function() print(test(t, f0)) end)))
print(test(t, f1))
print((pcall(function() print(test(t, f2)) end)))
print(test(t, f3))
print((pcall(function() print(test(t, f4)) end)))
print(test(t, f5))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 7 --')
debug.setmetatable(234, {
	__index = function(a,b,c,d)
		print("f3", a, b, c, d)
		return 433
	end
})
print(test(t, f0))
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 8 --')
debug.setmetatable(345, {})
print((pcall(function() print(test(t, f0)) end)))
print(test(t, f1))
print((pcall(function() print(test(t, f2)) end)))
print(test(t, f3))
print((pcall(function() print(test(t, f4)) end)))
print(test(t, f5))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 9 --')
t = false
print((pcall(function() print(test(t, f1)) end)))
print((pcall(function() print(test(t, f2)) end)))
print((pcall(function() print(test(t, f3)) end)))
print((pcall(function() print(test(t, f4)) end)))
print((pcall(function() print(test(t, f5)) end)))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 10 --')
debug.setmetatable(true, {
	__index = function(a,b,c)
		print("f3", a, b, c)
		return 455
	end
})
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 11 --')
debug.setmetatable(true, {
	__index = 566
})
print((pcall(function() print(test(t, f1)) end)))
print((pcall(function() print(test(t, f2)) end)))
print((pcall(function() print(test(t, f3)) end)))
print((pcall(function() print(test(t, f4)) end)))
print((pcall(function() print(test(t, f5)) end)))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 12 --')
debug.setmetatable(677, {
	__call = function(a,b,c,d)
		print("f4", a, b, c, d)
		return 788
	end
})
print((pcall(function() print(test(t, f1)) end)))
print((pcall(function() print(test(t, f2)) end)))
print((pcall(function() print(test(t, f3)) end)))
print((pcall(function() print(test(t, f4)) end)))
print((pcall(function() print(test(t, f5)) end)))
print((pcall(function() print(test(t, f6)) end)))
print((pcall(function() print(test(t, f7)) end)))


print('-- test 13 --')
debug.setmetatable(677, {
	__index = function(a,b,c,d)
		print("f5", a, b, c, d)
		return 899
	end
})
print(test(t, f1))
print(test(t, f2))
print(test(t, f3))
print(test(t, f4))
print(test(t, f5))
print(test(t, f6))
print(test(t, f7))


print('-- test 14 --')
t1 = { name = "t1", a1 = 1 , [21] = 4 }
t2 = { name = "t2", a2 = 2 , [22] = 5 }
t3 = { name = "t3", a3 = 3 , [23] = 6 }
t4 = { name = "t4", a4 = 4 , [24] = 7 }
t5 = { name = "t5", a5 = 5 , [25] = 8 }
t6 = { name = "t6", a6 = 6 , [26] = 9 }
t7 = { name = "t7", a7 = 7 , [27] = 10 }
t8 = { name = "t8", a8 = 8 , [28] = 11 }
t9 = { name = "t9", a9 = 9 , [29] = 12 }
t10 = { name = "t10", a10 = 10, [30] = 13, [false] = 14 }
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

print(test(t1, "a1"))
print(test(t1, "a2"))
print(test(t1, "a3"))
print(test(t1, "a4"))
print(test(t1, "a5"))
print(test(t1, "a6"))
print(test(t1, "a7"))
print(test(t1, "a8"))
print(test(t1, "a9"))
print(test(t1, "a10"))
print(test(t1, "a11"))

print(test(t1, 21))
print(test(t1, 22))
print(test(t1, 23))
print(test(t1, 24))
print(test(t1, 25))
print(test(t1, 26))
print(test(t1, 27))
print(test(t1, 28))
print(test(t1, 20))
print(test(t1, 30))
print(test(t1, 31))

print(test(t1, false))
print(test(t1, true))
print(test(t1, nil))
print(test(t1, 0.0/0))

print(test(t2, "a1"))
print(test(t2, "a2"))
print(test(t2, "a3"))
print(test(t2, "a11"))
print(test(t2, 0.0/0))

print(test(t2, 21))
print(test(t2, 22))
print(test(t2, 23))
print(test(t2, 21))

print(test(t2, false))
print(test(t2, true))
print(test(t2, nil))

print('-- test 15 --')

setmetatable(t10, { __index = 456 })

print(test(t1, "a10"))
print(test(t1, "a11"))
print(test(t1, 21))
print(test(t1, 31))
print(test(t1, false))
print(test(t1, true))
print(test(t1, nil))
print(test(t1, 0.0/0))

print(test(t2, "a10"))
print(test(t2, "a11"))
print(test(t2, 21))
print(test(t2, 31))
print(test(t2, false))
print(test(t2, true))
print(test(t2, nil))
print(test(t2, 0.0/0))

print('-- test 16 --')

t11 = { name = "t11", a11 = 11 }

debug.setmetatable(-123, {
	__index = t11
})

print(test(t1, "a10"))
print(test(t1, "a11"))
print(test(t1, 21))
print(test(t1, 31))
print(test(t1, false))
print(test(t1, true))
print(test(t1, nil))
print(test(t1, 0.0/0))

print(test(t2, "a10"))
print(test(t2, "a11"))
print(test(t2, 21))
print(test(t2, 31))
print(test(t2, false))
print(test(t2, true))
print(test(t2, nil))
print(test(t2, 0.0/0))

