print('-- test 1 --')

setmetatable(_G, {
	__newindex = function(a,b,c)
		print("f1",a == _G,b,c)
		rawset(a, b, c * 2)
		return 123
	end
})

print(x1)
print(y1)

x1 = 12
y1 = 34
print(x1)
print(y1)

x1 = 12
y1 = 34
print(x1)
print(y1)

print('-- test 2 --')

setmetatable(_G, nil)

t1 = { x2 = "xx", y2 = "yy" }
x2 = 34

setmetatable(_G, {
	__newindex = t1
})

print(x2)
print(y2)

x2 = 12
y2 = 23

print(x2)
print(y2)

print(t1.x2)
print(t1.y2)

print('-- test 3 --')

setmetatable(_G, nil)
x3 = 45
t2 = { x3 = "xxx", y3 = "yyy" }

setmetatable(_G, {
	__newindex = 233
})

debug.setmetatable(344, {
	__newindex = t2
})
	
print(x3)
print(y3)

x3 = 21
y3 = 32

print(x3)
print(y3)

print(t2.x3)
print(t2.y3)

print('-- test 4 --')
x3 = nil
y3 = nil
print(x3)
print(y3)
print(t2.x3)
print(t2.y3)

x3 = 43
y3 = 54
print(x3)
print(y3)
print(t2.x3)
print(t2.y3)

print('-- test 5 --')

setmetatable(_G, nil)

x4 = 65

debug.setmetatable(344, {
	__newindex = function(a,b,c,d)
		print("f2", a,b,c,d)
		rawset(_G,b,c * 3)
		return 233
	end
})

setmetatable(_G, {
	__newindex = 233
})

print(x4)
print(y4)

x4 = 76
y4 = 87

print(x4)
print(y4)

x4 = 98
y4 = 89

print(x4)
print(y4)

print('-- test 6 --')
debug.setmetatable(344, {})

print(x4)
x4 = 12
print(x4)

print((pcall(function() x5 = 1 end)))

print('-- test 7 --')
setmetatable(_G, nil)

t1 = { name = "t1", a1 = 1 }
t2 = { name = "t2", a1 = 2, a2 = 3 }
t3 = { name = "t3", a1 = 4, a2 = 5, a3 = 6 }
t4 = { name = "t4", a1 = 7, a2 = 8, a3 = 9, a4 = 10 }
t5 = { name = "t5", a1 = 11, a2 = 12, a3 = 13, a4 = 14, a5 = 15 }
t6 = { name = "t6", a1 = 16, a2 = 17, a3 = 18, a4 = 19, a5 = 20, a6 = 21 }
setmetatable(t1, { __newindex = t2 })
setmetatable(t2, { __newindex = t3 })
setmetatable(t3, { __newindex = t4 })
setmetatable(t4, { __newindex = t5 })
setmetatable(t5, { __newindex = t6 })
setmetatable(t6, { __newindex = function(a,b,c,d)
	print("f6", a.name, b ,c,d)
	rawset(a,b,c*100)
	return 900
end })

debug.setmetatable(455, { __newindex = t1 })
setmetatable(_G, { __newindex = 566 })

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)

a1 = 22
a2 = 23
a3 = 24
a4 = 25
a5 = 26
a6 = 27
a7 = 28

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7)

a1 = 29
a2 = 30
a3 = 31
a4 = 32
a5 = 33
a6 = 34
a7 = 35
a8 = 36

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)
print(a8)

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7, t1.a8)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7, t2.a8)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7, t3.a8)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7, t4.a8)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7, t5.a8)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7, t6.a8)


print('-- test 9 --')
setmetatable(_G, nil)

a1 = nil
a2 = nil
a3 = nil
a4 = nil
a5 = nil
a6 = nil
a7 = nil
a8 = nil

t1 = { name = "t1", a1 = 1 }
t2 = { name = "t2", a1 = 2, a2 = 3 }
t3 = { name = "t3", a1 = 4, a2 = 5, a3 = 6 }
t4 = { name = "t4", a1 = 7, a2 = 8, a3 = 9, a4 = 10 }
t5 = { name = "t5", a1 = 11, a2 = 12, a3 = 13, a4 = 14, a5 = 15 }
t6 = { name = "t6", a1 = 16, a2 = 17, a3 = 18, a4 = 19, a5 = 20, a6 = 21 }
setmetatable(t1, { __newindex = t2 })
setmetatable(t2, { __newindex = t3 })
setmetatable(t3, { __newindex = t4 })
setmetatable(t4, { __newindex = t5 })
setmetatable(t5, { __newindex = t6 })
setmetatable(t6, { __newindex = function(a,b,c,d)
	print("f6", a.name, b ,c,d)
	rawset(a,b,c*100)
	return 900
end })

setmetatable(_G, { __newindex = t1 })

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)

a1 = 22
a2 = 23
a3 = 24
a4 = 25
a5 = 26
a6 = 27
a7 = 28

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7)

a1 = 29
a2 = 30
a3 = 31
a4 = 32
a5 = 33
a6 = 34
a7 = 35
a8 = 36

print(a1)
print(a2)
print(a3)
print(a4)
print(a5)
print(a6)
print(a7)
print(a8)

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7, t1.a8)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7, t2.a8)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7, t3.a8)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7, t4.a8)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7, t5.a8)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7, t6.a8)


