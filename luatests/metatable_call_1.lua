function f1(a, b, c, d)
	print(a.name,b,c,d)
	return 1,2,3
end

t1 = { name = "t1" }
setmetatable(t1, {
	__call = f1
})
print(t1(1))

function f2(a, b, c, d)
	print(a,b,c,d)
	return 2,3,4
end

debug.setmetatable("2", {
	__call = f2
})
print(("3")(4))

t2 = nil

debug.setmetatable(nil, {
	__call = f2
})
print(t2(5))

debug.setmetatable(false, {
	__call = f2
})

t3 = false
print(t3(6))

t3 = true
print(t3(7))

print((pcall(function()
	local t4 = {}
	t4(123)
end)))

t5 = {}
setmetatable(t5, {
	__call = "233"
})
print((pcall(function()
	-- recursive __call is not supported in Lua 5.1 so this should fail
	t5(123)
end)))

getmetatable(nil).__call = nil
print((pcall(function()
	t2(1234)
end)))

getmetatable(false).__call = nil
print((pcall(function()
	t3(1234)
end)))

