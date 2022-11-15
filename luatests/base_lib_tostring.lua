a={ name = "xxx" }
setmetatable(a, {
	__tostring = function(t)
		return t.name
	end
})

print(tostring(a))
print(a)
a.name = "yyy"
print(tostring(a))
print(a)

debug.setmetatable(1, {
	__tostring = function(t)
		return t + 100
	end
})

print("a", 123, "b", 234, 345)

debug.setmetatable("a", {
	__tostring = function(t)
		return t .. "xx"
	end
})

print("a", 123, "b", 234, 345)


