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
		print('enter concat b', lhs.name, rhs)
		return "qwerty"
	end
})

concat("x", "789", 345.6, "123", b, 234)

concat("789", 345.6, "123", b, 234, "x")

concat("123", b, 234, "x", "789", 345.6)

concat("x", b, 345.6, "123", b, 234)

setmetatable(a, {
	__concat = b
})
setmetatable(b, {
	__call = function(v1, v2, v3)
		print('enter concat b', v1.name, v2.name, v3)
		return "qwerty"
	end
})

concat("x", "789", 345.6, "123", a, 234)

concat("789", 345.6, "123", a, 234, "x")

concat("123", a, 234, "x", "789", 345.6)

concat("x", a, 345.6, "123", a, 234)


