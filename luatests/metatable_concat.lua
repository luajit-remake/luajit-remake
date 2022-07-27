function concat(a, b, c, d, e, f)
	local res = a .. b .. c .. d .. e .. f 
	print(res)
	print()
end

local b = { name = 'b' }
local a = { name = 'a' }
setmetatable(a, {
	__concat = function(lhs, rhs)
		print('enter concat a', lhs.name, rhs)
		return b
	end
})
setmetatable(b, {
	__concat = function(lhs, rhs)
		print('enter concat b', lhs, rhs.name)
		return "qwerty"
	end
})

concat("x", "789", 345.6, "123", a, 234)

concat("789", 345.6, "123", a, 234, "x")

concat("123", a, 234, "x", "789", 345.6)

concat("x", a, 345.6, "123", a, 234)

debug.setmetatable("www", {
	__concat = function(lhs, rhs)
		print('enter concat str', lhs, rhs.name)
		return "asdf"
	end
})

concat("x", "789", 345.6, "123", a, 234)

concat("789", 345.6, "123", a, 234, "x")

concat("123", a, 234, "x", "789", 345.6)

concat("x", a, 345.6, "123", a, 234)

debug.setmetatable(12, {
	__len = function(x)
		print('enter len', x)
		return 67
	end
})

local c = { name = 'c' }
setmetatable(c, {
	__concat = function(lhs, rhs)
		print('enter concat c', lhs.name, rhs, #rhs)
		return
	end
})
print(c .. 1234)
print()

local d = { name = 'd' }
setmetatable(d, {
	__concat = function(lhs, rhs)
		print('enter concat d', lhs, rhs.name, #lhs)
		return
	end
})
print(1234 .. d)
print()

-- test some error cases

debug.setmetatable("wwwww", nil)

print((pcall(function()
	concat("x", "789", {}, "123", a, 234)
end)))
print()

print((pcall(function()
	concat({}, "123", a, 234, "789", "x")
end)))
print()

print((pcall(function()
	print({} .. {})
end)))
print('ok')

