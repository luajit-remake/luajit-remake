t = {}

setmetatable(t, {
	__index = function()
		print("should never reach here")
	end,
	__newindex = function()
		print("should never reach here")
	end
})

t2 = { x = 'y' }

a1 = 0.0
a2 = a1 / a1
a3 = -a1

print('-- rawget --')
print(rawget(t, "a"))
print(rawget(t, 123))
print(rawget(t, 45.6))
print(rawget(t, nil))
print(rawget(t, false))
print(rawget(t, true))
print(rawget(t, t2))
print(rawget(t, t))
print(rawget(t, a1))
print(rawget(t, a2))
print(rawget(t, a3))

print('-- rawset --')
rawset(t, "a", 1)
rawset(t, 123, 2)
rawset(t, 45.6, 3)
print((pcall(function() rawset(t, nil, 4) end)))
rawset(t, false, 5)
rawset(t, true, 6)
rawset(t, t2, 7)
rawset(t, t, 8)
rawset(t, a1, 9)
print((pcall(function() rawset(t, a2, 10) end)))
rawset(t, a3, 11)

print('-- rawget --')
print(rawget(t, "a"))
print(rawget(t, 123))
print(rawget(t, 45.6))
print(rawget(t, nil))
print(rawget(t, false))
print(rawget(t, true))
print(rawget(t, t2))
print(rawget(t, t))
print(rawget(t, a1))
print(rawget(t, a2))
print(rawget(t, a3))

print('-- error cases --')
print((pcall(function() rawget() end)))
print((pcall(function() rawget(t) end)))
print((pcall(function() rawset() end)))
print((pcall(function() rawset(t) end)))
print((pcall(function() rawset(t, "1") end)))
print((pcall(function() rawget("x", "a") end)))
print((pcall(function() rawget(123, "a") end)))
print((pcall(function() rawset("x", "a", "a") end)))
print((pcall(function() rawset(123, "a", "a") end)))


