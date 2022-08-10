print('-- test 1 --')

cnt = 0
t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print("should never reach here")
	end
})

rawset(t, 1, 123)
print(t[1])
t[1] = 234
print(t[1])

rawset(t, 2, 321)
print(t[2])
t[2] = 432
print(t[2])

rawset(t, 4, 345)
print(t[4])
t[4] = 456
print(t[4])

rawset(t, 100, 567)
print(t[100])
t[100] = 678
print(t[100])

print('-- test 2 --')

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

cnt = 0
for i = 1,6 do
	print("before", i, t[1])
	t[1] = 123
	print("after", i, t[1])
end

cnt = 0
for i = 1,6 do
	print("before", i, t[2])
	t[2] = 234
	print("after", i, t[2])
end

cnt = 0
for i = 1,6 do
	print("before", i, t[4])
	t[4] = 345
	print("after", i, t[4])
end

cnt = 0
for i = 1,6 do
	print("before", i, t[100])
	t[100] = 456
	print("after", i, t[100])
end

print('-- test 3 --')

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
t[1] = 20
print(t[1])
t[1] = 30
print(t[1])

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
t[2] = 21
print(t[2])
t[2] = 31
print(t[2])

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
t[4] = 22
print(t[4])
t[4] = 32
print(t[4])

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
t[100] = 23
print(t[100])
t[100] = 33
print(t[100])

print('-- test 4 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = 233
})

print((pcall(function() t[1] = 1 end)))

debug.setmetatable(233, {
	__newindex = function(a,b,c,d,e)
		print("f3", a,b,c,d,e)
	end
})
t[1] = 12
print(t[1])
t[1] = 23
print(t[1])


print('-- test 5 --')

t1 = { name = "t1", [1] = 1 }
t2 = { name = "t2", [1] = 2, [2] = 3 }
t3 = { name = "t3", [1] = 4, [2] = 5, [3] = 6 }
t4 = { name = "t4", [1] = 7, [2] = 8, [3] = 9, [4] = 10 }
t5 = { name = "t5", [1] = 11, [2] = 12, [3] = 13, [4] = 14, [5] = 15 }
t6 = { name = "t6", [1] = 16, [2] = 17, [3] = 18, [4] = 19, [5] = 20, [6] = 21 }
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

t1[1] = 22
t1[2] = 23
t1[3] = 24
t1[4] = 25
t1[5] = 26
t1[6] = 27
t1[7] = 28

print(t1.name, t1[1], t1[2], t1[3], t1[4], t1[5], t1[6], t1[7])
print(t2.name, t2[1], t2[2], t2[3], t2[4], t2[5], t2[6], t2[7])
print(t3.name, t3[1], t3[2], t3[3], t3[4], t3[5], t3[6], t3[7])
print(t4.name, t4[1], t4[2], t4[3], t4[4], t4[5], t4[6], t4[7])
print(t5.name, t5[1], t5[2], t5[3], t5[4], t5[5], t5[6], t5[7])
print(t6.name, t6[1], t6[2], t6[3], t6[4], t6[5], t6[6], t6[7])

t1[1] = 29
t1[2] = 30
t1[3] = 31
t1[4] = 32
t1[5] = 33
t1[6] = 34
t1[7] = 35
t1[8] = 36

print(t1.name, t1[1], t1[2], t1[3], t1[4], t1[5], t1[6], t1[7], t1[8])
print(t2.name, t2[1], t2[2], t2[3], t2[4], t2[5], t2[6], t2[7], t2[8])
print(t3.name, t3[1], t3[2], t3[3], t3[4], t3[5], t3[6], t3[7], t3[8])
print(t4.name, t4[1], t4[2], t4[3], t4[4], t4[5], t4[6], t4[7], t4[8])
print(t5.name, t5[1], t5[2], t5[3], t5[4], t5[5], t5[6], t5[7], t5[8])
print(t6.name, t6[1], t6[2], t6[3], t6[4], t6[5], t6[6], t6[7], t6[8])


print('-- test 6 --')

t1 = { name = "t1", [1] = 1 }
t2 = { name = "t2", [1] = 2, [2] = 3 }
t3 = { name = "t3", [1] = 4, [2] = 5, [3] = 6 }
t4 = { name = "t4", [1] = 7, [2] = 8, [3] = 9, [4] = 10 }
t5 = { name = "t5", [1] = 11, [2] = 12, [3] = 13, [4] = 14, [5] = 15 }
t6 = { name = "t6", [1] = 16, [2] = 17, [3] = 18, [4] = 19, [5] = 20, [6] = 21 }
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

t1[1] = 22
t1[2] = 23
t1[3] = 24
t1[4] = 25
t1[5] = 26
t1[6] = 27
t1[7] = 28

print(t1.name, t1[1], t1[2], t1[3], t1[4], t1[5], t1[6], t1[7])
print(t2.name, t2[1], t2[2], t2[3], t2[4], t2[5], t2[6], t2[7])
print(t3.name, t3[1], t3[2], t3[3], t3[4], t3[5], t3[6], t3[7])
print(t4.name, t4[1], t4[2], t4[3], t4[4], t4[5], t4[6], t4[7])
print(t5.name, t5[1], t5[2], t5[3], t5[4], t5[5], t5[6], t5[7])
print(t6.name, t6[1], t6[2], t6[3], t6[4], t6[5], t6[6], t6[7])

t1[1] = 29
t1[2] = 30
t1[3] = 31
t1[4] = 32
t1[5] = 33
t1[6] = 34
t1[7] = 35
t1[8] = 36

print(t1.name, t1[1], t1[2], t1[3], t1[4], t1[5], t1[6], t1[7], t1[8])
print(t2.name, t2[1], t2[2], t2[3], t2[4], t2[5], t2[6], t2[7], t2[8])
print(t3.name, t3[1], t3[2], t3[3], t3[4], t3[5], t3[6], t3[7], t3[8])
print(t4.name, t4[1], t4[2], t4[3], t4[4], t4[5], t4[6], t4[7], t4[8])
print(t5.name, t5[1], t5[2], t5[3], t5[4], t5[5], t5[6], t5[7], t5[8])
print(t6.name, t6[1], t6[2], t6[3], t6[4], t6[5], t6[6], t6[7], t6[8])


