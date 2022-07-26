a = { name = "a" }

f0 = function(a,b,c)
	print(a.name, b, c)
	return 12, 34
end

setmetatable(a, {
	__add = f0
})

print(a+2333)
print(a+"2333")

f1 = function(a,b,c)
	print(a, b.name, c)
	return 43, 21
end

setmetatable(a, {
	__add = f1
})
print(321 + a)
print("cba" + a)

b = { name = "b" }

f2 = function(a,b,c)
	print("f2", a.name, b.name, c)
	return 67, 89
end

f3 = function(a,b,c)
	print("f3", a.name, b.name, c)
	return 98, 76
end

setmetatable(a, {
	__add = f2
})

setmetatable(b, {
	__add = f3
})
print(a+b)
print(b+a)

setmetatable(b, {
	__call = true
})
print(a+b)
print(b+a)

setmetatable(a, {
	__add = false
})

f4 = function(a, b, c, d)
	print(a,b.name,c, d)
	return 123,456
end

debug.setmetatable(true, { __call = f4 })
print(a+233)
print(a+"abb")

f5 = function(a, b, c, d)
	print(a,b,c.name, d)
	return 124, 567
end

debug.setmetatable(true, { __call = f5 })

print(233+a)
print("233"+a)

c = { name = "c" }
setmetatable(a, {
	__add = c
})

f6 = function(a, b, c, d)
	print(a.name,b.name,c, d)
	return 98, 76
end

f7 = function(a, b, c, d)
	print(a.name,b,c.name, d)
	return 87, 65
end

setmetatable(c, {
	__call = f6
})
print(a+234)
print(a+"xyz")

setmetatable(c, {
	__call = f7
})
print(432 + a)
print("zwx" + a)

print((pcall(function() 
	t1 = {}
	t2 = {}
	return t1 + t2
end)))

print((pcall(function() 
	t1 = "abc"
	t2 = "def"
	return t1 + t2
end)))

t3 = "123"
t4 = "  0x234    "
print(t3 + t4)

print(t3 + 15)
print(16 + t3)
print(t4 + 951)
print(159 + t4)


