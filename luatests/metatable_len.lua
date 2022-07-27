a = { 1, 2, 3, 4, 5, 6, x = 1 }
print(#a)
a[7] = 7
print(#a)

function f(a, b, c, d)
	print(a, b, c, d)
	return 123, 456
end

setmetatable(a, { __len = f })
print(#a)

b = "123454321"
print(#b)
debug.setmetatable("x", { __len = f })
print(#b)
debug.setmetatable("x", nil)
print(#b)

c = true
print((pcall(function() print(#c) end)))
debug.setmetatable(false, { __len = f })
print(#c)
c = false
print(#c)
debug.setmetatable(false, nil)
print((pcall(function() print(#c) end)))

debug.setmetatable("x", { __call = f })
debug.setmetatable(false, { __len = "abc" })
print(#c)
debug.setmetatable("x", nil)
print((pcall(function() print(#c) end)))

