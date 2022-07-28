t1 = "123"
t2 = 123

print(t1 == t2)
print(t1 ~= t2)

debug.setmetatable(123, {
	__eq = function(a,b)
		print("should never reach here")
	end
})

debug.setmetatable("123", {
	__eq = function(a,b)
		print("should never reach here")
	end
})

print(t1 == t2)
print(t1 ~= t2)

t1 = function(x,y) return x end
t2 = t1

print(t1 == t2)
print(t1 ~= t2)

debug.setmetatable(function(x,y) end, {
	__eq = function(a,b)
		print("should never reach here")
	end
})

print(t1 == t2)
print(t1 ~= t2)

t2 = function(x,y) return y end
print(t1 == t2)
print(t1 ~= t2)

-- Lua 5.1 & 5.2 specific: test that metamethod is only called when lhs & rhs has same metamethod 

t1 = {}
setmetatable(t1, {
	__eq = function(a,b)
		print("should never reach here")
		return a
	end
})

t2 = {}
setmetatable(t2, {
	__eq = function(a,b)
		print("should never reach here")
		return b
	end
})

print(t1 == t2)
print(t1 ~= t2)

