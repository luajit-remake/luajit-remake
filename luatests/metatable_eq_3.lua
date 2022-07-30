t1 = { name = "t1" }
t2 = { name = "t2" }
setmetatable(t1, {})
setmetatable(t2, {})

print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

function g(a, b)
	print("should never reach here")
end

function f(a, b)
	print("f",a.name,b.name)
	return 123
end

getmetatable(t1).__lt = g
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t2).__lt = g
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t1).__eq = f
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t2).__eq = f
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

function f2(a, b)
	print("f2",a.name,b.name)
	return false
end

getmetatable(t1).__eq = f2
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t2).__eq = f2
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t1).__eq = f
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

getmetatable(t2).__eq = f
print(t1 == t1)
print(t1 ~= t1)
print(t1 == t2)
print(t1 ~= t2)
print(t2 == t2)
print(t2 ~= t2)

