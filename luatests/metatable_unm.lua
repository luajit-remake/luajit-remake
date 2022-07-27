print((pcall(function() local x = {} print(-x) end)))

a = { name = "a" }

f = function(a,b,c)
	print(a.name, b.name, c)
	return 12, 34
end

setmetatable(a, {
	__unm = f
})

print(-a)

f2 = function(a,b,c)
	print(a.name, b.name, c)
	return 
end

setmetatable(a, {
	__unm = f2
})

print(-a)

print((pcall(function() local x = "" print(-x) end)))

x = 233
print(-x)

x = "233"
print(-x)

x = " 0x233  "
print(-x)

print((pcall(function() local x = "0x233G" print(-x) end)))

f3 = function(a,b,c)
	print(a, b, c)
	return 
end

debug.setmetatable("x", { __unm = f3 })

x = "233"
print(-x)

x = " 0x233  "
print(-x)

x = "233"
print(-x)

x = " 0x233  "
print(-x)

x = "0x233G"
print(-x)

f4 = function(a,b,c,d)
	print(a.name,b.name,c.name,d)
	return 1234, 5678
end

b = { name = "b" }
setmetatable(b, { __call = f4 })

setmetatable(a, { __unm = b })
print(-a)

setmetatable(b, nil)
print((pcall(function() print(-a) end)))

f5 = function(a,b,c,d)
	print(a, b.name, c.name, d)
	return
end

debug.setmetatable("y", { __call = f5 })

setmetatable(a, { __unm = "xxxx" })
print(-a)

