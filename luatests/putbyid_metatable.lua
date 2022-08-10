print('-- test 1 --')

cnt = 0
t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print("should never reach here")
	end
})

rawset(t, "a", 123)
print(i, t.a)
t.a = 234
print(i, t.a)

print('-- test 2 --')

cnt = 0
t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print(a.name, b, c, d)
		cnt = cnt + 1
		if cnt == 3 then
			rawset(a, b, c * 2)
		end
		return
	end
})

for i = 1,6 do
	print("before", i, t.a)
	t.a = 123
	print("after", i, t.a)
end

print('-- test 2 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print("f1", a.name, b, c, d)
		setmetatable(a, {
			__newindex = function(a, b, c, d) 
				print("f2", a.name, b, c, d)
				setmetatable(a, {})
				a[b] = c * 3
			end
		})
		a.xx = c * 2
	end
})
t.xx = 20
print(t.xx)
t.xx = 30
print(t.xx)

print('-- test 3 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = 233
})

print((pcall(function() t.yy = 1 end)))

debug.setmetatable(233, {
	__newindex = function(a,b,c,d,e)
		print("f3", a,b,c,d,e)
	end
})
t.zz = 12
print(t.zz)
t.zz = 23
print(t.zz)


print('-- test 4 --')

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

debug.setmetatable(455, { __newindex = t2 })
setmetatable(t1, { __newindex = 566 })

t1.a1 = 22
t1.a2 = 23
t1.a3 = 24
t1.a4 = 25
t1.a5 = 26
t1.a6 = 27
t1.a7 = 28

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7)

t1.a1 = 29
t1.a2 = 30
t1.a3 = 31
t1.a4 = 32
t1.a5 = 33
t1.a6 = 34
t1.a7 = 35
t1.a8 = 36

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7, t1.a8)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7, t2.a8)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7, t3.a8)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7, t4.a8)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7, t5.a8)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7, t6.a8)


print('-- test 5 --')

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
	print("f7", a.name, b ,c,d)
	rawset(a,b,c*100)
	return 900
end })

t1.a1 = 22
t1.a2 = 23
t1.a3 = 24
t1.a4 = 25
t1.a5 = 26
t1.a6 = 27
t1.a7 = 28

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7)

t1.a1 = 29
t1.a2 = 30
t1.a3 = 31
t1.a4 = 32
t1.a5 = 33
t1.a6 = 34
t1.a7 = 35
t1.a8 = 36

print(t1.name, t1.a1, t1.a2, t1.a3, t1.a4, t1.a5, t1.a6, t1.a7, t1.a8)
print(t2.name, t2.a1, t2.a2, t2.a3, t2.a4, t2.a5, t2.a6, t2.a7, t2.a8)
print(t3.name, t3.a1, t3.a2, t3.a3, t3.a4, t3.a5, t3.a6, t3.a7, t3.a8)
print(t4.name, t4.a1, t4.a2, t4.a3, t4.a4, t4.a5, t4.a6, t4.a7, t4.a8)
print(t5.name, t5.a1, t5.a2, t5.a3, t5.a4, t5.a5, t5.a6, t5.a7, t5.a8)
print(t6.name, t6.a1, t6.a2, t6.a3, t6.a4, t6.a5, t6.a6, t6.a7, t6.a8)


