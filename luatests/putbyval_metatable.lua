test = function(t, i, val)
	t[i] = val
end

f0 = "a"
f1 = "b"
f2 = 1.2
f3 = 3
f4 = -5
f5 = false
f6 = 0.0/0
f7 = nil

print('-- test 1 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = function(a,b,c,d)
		print("f1",a.name,b,c,d)
		rawset(a, b, c * 2)
	end
})

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t[f0])
print(t[f1])
print(t[f2])
print(t[f3])
print(t[f4])
print(t[f5])
print(t[f6])
print(t[f7])

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t[f0])
print(t[f1])
print(t[f2])
print(t[f3])
print(t[f4])
print(t[f5])
print(t[f6])
print(t[f7])

print('-- test 2 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = 233
})
debug.setmetatable(344, {
	__newindex = function(a,b,c,d)
		print("f2",a,b,c,d)
		rawset(t, b, c * 3)
	end
})

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t[f0])
print(t[f1])
print(t[f2])
print(t[f3])
print(t[f4])
print(t[f5])
print(t[f6])
print(t[f7])

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t[f0])
print(t[f1])
print(t[f2])
print(t[f3])
print(t[f4])
print(t[f5])
print(t[f6])
print(t[f7])

print('-- test 3 --')

f8 = 0

t0 = { name = "t0" }
t1 = { name = "t1", [f0] = 1 }
t2 = { name = "t2", [f0] = 2, [f1] = 3 }
t3 = { name = "t3", [f0] = 4, [f1] = 5, [f2] = 6 }
t4 = { name = "t4", [f0] = 7, [f1] = 8, [f2] = 9, [f3] = 10 }
t5 = { name = "t5", [f0] = 11, [f1] = 12, [f2] = 13, [f3] = 14, [f4] = 15 }
t6 = { name = "t6", [f0] = 16, [f1] = 17, [f2] = 18, [f3] = 19, [f4] = 20, [f5] = 21 }
setmetatable(t0, { __newindex = t1 })
setmetatable(t1, { __newindex = t2 })
setmetatable(t2, { __newindex = t3 })
setmetatable(t3, { __newindex = t4 })
setmetatable(t4, { __newindex = t5 })
setmetatable(t5, { __newindex = t6 })
setmetatable(t6, { __newindex = function(a,b,c,d)
	print("f3", a.name, b ,c,d)
	rawset(a,b,c*100)
	return 900
end })

debug.setmetatable(455, { __newindex = t1 })
setmetatable(t0, { __newindex = 566 })

test(t0, f0, 101)
test(t0, f1, 102)
test(t0, f2, 103)
test(t0, f3, 104)
test(t0, f4, 105)
test(t0, f5, 106)
test(t0, f8, 107)
print((pcall(function() test(t0, f6, 108) end)))
print((pcall(function() test(t0, f7, 109) end)))

print(t0.name, t0[f0], t0[f1], t0[f2], t0[f3], t0[f4], t0[f5], t0[f8], t0[f6], t0[f7])
print(t1.name, t1[f0], t1[f1], t1[f2], t1[f3], t1[f4], t1[f5], t1[f8], t1[f6], t1[f7])
print(t2.name, t2[f0], t2[f1], t2[f2], t2[f3], t2[f4], t2[f5], t2[f8], t2[f6], t2[f7])
print(t3.name, t3[f0], t3[f1], t3[f2], t3[f3], t3[f4], t3[f5], t3[f8], t3[f6], t3[f7])
print(t4.name, t4[f0], t4[f1], t4[f2], t4[f3], t4[f4], t4[f5], t4[f8], t4[f6], t4[f7])
print(t5.name, t5[f0], t5[f1], t5[f2], t5[f3], t5[f4], t5[f5], t5[f8], t5[f6], t5[f7])
print(t6.name, t6[f0], t6[f1], t6[f2], t6[f3], t6[f4], t6[f5], t6[f8], t6[f6], t6[f7])

test(t0, f0, 101)
test(t0, f1, 102)
test(t0, f2, 103)
test(t0, f3, 104)
test(t0, f4, 105)
test(t0, f5, 106)
test(t0, f8, 107)
print((pcall(function() test(t0, f6, 108) end)))
print((pcall(function() test(t0, f7, 109) end)))

print(t0.name, t0[f0], t0[f1], t0[f2], t0[f3], t0[f4], t0[f5], t0[f8], t0[f6], t0[f7])
print(t1.name, t1[f0], t1[f1], t1[f2], t1[f3], t1[f4], t1[f5], t1[f8], t1[f6], t1[f7])
print(t2.name, t2[f0], t2[f1], t2[f2], t2[f3], t2[f4], t2[f5], t2[f8], t2[f6], t2[f7])
print(t3.name, t3[f0], t3[f1], t3[f2], t3[f3], t3[f4], t3[f5], t3[f8], t3[f6], t3[f7])
print(t4.name, t4[f0], t4[f1], t4[f2], t4[f3], t4[f4], t4[f5], t4[f8], t4[f6], t4[f7])
print(t5.name, t5[f0], t5[f1], t5[f2], t5[f3], t5[f4], t5[f5], t5[f8], t5[f6], t5[f7])
print(t6.name, t6[f0], t6[f1], t6[f2], t6[f3], t6[f4], t6[f5], t6[f8], t6[f6], t6[f7])

print('-- test 4 --')

debug.setmetatable(455, {})
f8 = 0

t0 = { name = "t0" }
t1 = { name = "t1", [f0] = 1 }
t2 = { name = "t2", [f0] = 2, [f1] = 3 }
t3 = { name = "t3", [f0] = 4, [f1] = 5, [f2] = 6 }
t4 = { name = "t4", [f0] = 7, [f1] = 8, [f2] = 9, [f3] = 10 }
t5 = { name = "t5", [f0] = 11, [f1] = 12, [f2] = 13, [f3] = 14, [f4] = 15 }
t6 = { name = "t6", [f0] = 16, [f1] = 17, [f2] = 18, [f3] = 19, [f4] = 20, [f5] = 21 }
setmetatable(t0, { __newindex = t1 })
setmetatable(t1, { __newindex = t2 })
setmetatable(t2, { __newindex = t3 })
setmetatable(t3, { __newindex = t4 })
setmetatable(t4, { __newindex = t5 })
setmetatable(t5, { __newindex = t6 })
setmetatable(t6, { __newindex = function(a,b,c,d)
	print("f4", a.name, b ,c,d)
	rawset(a,b,c*100)
	return 900
end })

test(t0, f0, 101)
test(t0, f1, 102)
test(t0, f2, 103)
test(t0, f3, 104)
test(t0, f4, 105)
test(t0, f5, 106)
test(t0, f8, 107)
print((pcall(function() test(t0, f6, 108) end)))
print((pcall(function() test(t0, f7, 109) end)))

print(t0.name, t0[f0], t0[f1], t0[f2], t0[f3], t0[f4], t0[f5], t0[f8], t0[f6], t0[f7])
print(t1.name, t1[f0], t1[f1], t1[f2], t1[f3], t1[f4], t1[f5], t1[f8], t1[f6], t1[f7])
print(t2.name, t2[f0], t2[f1], t2[f2], t2[f3], t2[f4], t2[f5], t2[f8], t2[f6], t2[f7])
print(t3.name, t3[f0], t3[f1], t3[f2], t3[f3], t3[f4], t3[f5], t3[f8], t3[f6], t3[f7])
print(t4.name, t4[f0], t4[f1], t4[f2], t4[f3], t4[f4], t4[f5], t4[f8], t4[f6], t4[f7])
print(t5.name, t5[f0], t5[f1], t5[f2], t5[f3], t5[f4], t5[f5], t5[f8], t5[f6], t5[f7])
print(t6.name, t6[f0], t6[f1], t6[f2], t6[f3], t6[f4], t6[f5], t6[f8], t6[f6], t6[f7])

test(t0, f0, 101)
test(t0, f1, 102)
test(t0, f2, 103)
test(t0, f3, 104)
test(t0, f4, 105)
test(t0, f5, 106)
test(t0, f8, 107)
print((pcall(function() test(t0, f6, 108) end)))
print((pcall(function() test(t0, f7, 109) end)))

print(t0.name, t0[f0], t0[f1], t0[f2], t0[f3], t0[f4], t0[f5], t0[f8], t0[f6], t0[f7])
print(t1.name, t1[f0], t1[f1], t1[f2], t1[f3], t1[f4], t1[f5], t1[f8], t1[f6], t1[f7])
print(t2.name, t2[f0], t2[f1], t2[f2], t2[f3], t2[f4], t2[f5], t2[f8], t2[f6], t2[f7])
print(t3.name, t3[f0], t3[f1], t3[f2], t3[f3], t3[f4], t3[f5], t3[f8], t3[f6], t3[f7])
print(t4.name, t4[f0], t4[f1], t4[f2], t4[f3], t4[f4], t4[f5], t4[f8], t4[f6], t4[f7])
print(t5.name, t5[f0], t5[f1], t5[f2], t5[f3], t5[f4], t5[f5], t5[f8], t5[f6], t5[f7])
print(t6.name, t6[f0], t6[f1], t6[f2], t6[f3], t6[f4], t6[f5], t6[f8], t6[f6], t6[f7])

print('-- test 5 --')

t = 765

debug.setmetatable(12, {
	__newindex = function(a,b,c,d)
		print("f5",a,b,c,d)
	end
})

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
test(t, f6, 7)
test(t, f7, 8)

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
test(t, f6, 7)
test(t, f7, 8)

print('-- test 6 --')

t = 876

t2 = { name = "t2" }

debug.setmetatable(12, {
	__newindex = t2
})

setmetatable(t2, {
	__newindex = function(a,b,c,d)
		print("f6",a.name,b,c,d)
		rawset(a, b, c * 4)
	end
})

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t2[f0])
print(t2[f1])
print(t2[f2])
print(t2[f3])
print(t2[f4])
print(t2[f5])
print(t2[f6])
print(t2[f7])

test(t, f0, 1)
test(t, f1, 2)
test(t, f2, 3)
test(t, f3, 4)
test(t, f4, 5)
test(t, f5, 6)
print((pcall(function() test(t, f6, 7) end)))
print((pcall(function() test(t, f7, 8) end)))

print(t2[f0])
print(t2[f1])
print(t2[f2])
print(t2[f3])
print(t2[f4])
print(t2[f5])
print(t2[f6])
print(t2[f7])

print('-- test 7 --')

t = 876

t2 = { name = "t2" }

debug.setmetatable(12, {
	__newindex = t2
})

setmetatable(t2, {
	__newindex = function(a,b,c,d)
		print("f7",a.name,b,c,d)
		rawset(a, b, c * 4)
	end
})

t.a = 1
t.b = 2
t[1.2] = 3
t[3] = 4
t[-5] = 5
t[false] = 6
print((pcall(function() t[0.0/0] = 7 end)))
print((pcall(function() t[nil] = 8 end)))

print(t2[f0])
print(t2[f1])
print(t2[f2])
print(t2[f3])
print(t2[f4])
print(t2[f5])
print(t2[f6])
print(t2[f7])

t.a = 1
t.b = 2
t[1.2] = 3
t[3] = 4
t[-5] = 5
t[false] = 6
print((pcall(function() t[0.0/0] = 7 end)))
print((pcall(function() t[nil] = 8 end)))

print(t2[f0])
print(t2[f1])
print(t2[f2])
print(t2[f3])
print(t2[f4])
print(t2[f5])
print(t2[f6])
print(t2[f7])

print('-- test 8 --')

t = 876

debug.setmetatable(12, {
	__newindex = function(a,b,c,d)
		print("f8",a,b,c,d)
	end
})

t.a = 1
t.b = 2
t[1.2] = 3
t[3] = 4
t[-5] = 5
t[false] = 6
t[0.0/0] = 7
t[nil] = 8


t.a = 1
t.b = 2
t[1.2] = 3
t[3] = 4
t[-5] = 5
t[false] = 6
t[0.0/0] = 7
t[nil] = 8


