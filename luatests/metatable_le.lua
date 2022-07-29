print('-- test 1 --')
print((pcall(function() local a = nil; local b = nil; print(a<=b); end)))
print((pcall(function() local a = nil; local b = false; print(a<=b); end)))
print((pcall(function() local a = false; local b = true; print(a<=b); end)))

print((pcall(function() local a = nil; local b = nil; print(not(a<=b)); end)))
print((pcall(function() local a = nil; local b = false; print(not(a<=b)); end)))
print((pcall(function() local a = false; local b = true; print(not(a<=b)); end)))

print('-- test 2 --')
debug.setmetatable(nil, {
	__le = function(a,b)
		print("mt_nil", a,b)
		return 123
	end
})
local a = nil; local b = nil; print(a<=b);
print((pcall(function() local a = nil; local b = false; print(a<=b); end)))
print((pcall(function() local a = false; local b = true; print(a<=b); end)))

local a = nil; local b = nil; print(not(a<=b));
print((pcall(function() local a = nil; local b = false; print(not(a<=b)); end)))
print((pcall(function() local a = false; local b = true; print(not(a<=b)); end)))

print('-- test 3 --')
debug.setmetatable(nil, {
	__le = function(a,b)
		print("mt_nil_2", a,b)
		return 
	end
})
local a = nil; local b = nil; print(a<=b);
print((pcall(function() local a = nil; local b = false; print(a<=b); end)))
print((pcall(function() local a = false; local b = true; print(a<=b); end)))

local a = nil; local b = nil; print(not(a<=b));
print((pcall(function() local a = nil; local b = false; print(not(a<=b)); end)))
print((pcall(function() local a = false; local b = true; print(not(a<=b)); end)))

print('-- test 4 --')
debug.setmetatable(false, {
	__le = function(a,b)
		print("mt_bool", a,b)
		return 321
	end
})
local a = nil; local b = nil; print(a<=b);
print((pcall(function() local a = nil; local b = false; print(a<=b); end)))
local a = false; local b = true; print(a<=b); 

local a = nil; local b = nil; print(not(a<=b));
print((pcall(function() local a = nil; local b = false; print(not(a<=b)); end)))
local a = false; local b = true; print(not(a<=b)); 

print('-- test 5 --')
debug.setmetatable(false, {
	__le = function(a,b)
		print("mt_bool_2", a,b)
		return false
	end
})
local a = nil; local b = nil; print(a<=b);
print((pcall(function() local a = nil; local b = false; print(a<=b); end)))
local a = false; local b = true; print(a<=b); 

local a = nil; local b = nil; print(not(a<=b));
print((pcall(function() local a = nil; local b = false; print(not(a<=b)); end)))
local a = false; local b = true; print(not(a<=b)); 

print('-- test 6 --')
local a = "abc"; local b = "abcd"; print(a<=b)
local a = "abcd"; local b = "abc"; print(a<=b)
local a = "abc"; local b = "abc"; print(a<=b)
local a = "abcd"; local b = "abce"; print(a<=b)

local a = "abc"; local b = "abcd"; print(not(a<=b))
local a = "abcd"; local b = "abc"; print(not(a<=b))
local a = "abc"; local b = "abc"; print(not(a<=b))
local a = "abcd"; local b = "abce"; print(not(a<=b))

print('-- test 7 --')

debug.setmetatable("1", {
	__le = function(a,b)
		print("should never reach here")
	end
})
local a = "abc"; local b = "abcd"; print(a<=b)
local a = "abcd"; local b = "abc"; print(a<=b)
local a = "abc"; local b = "abc"; print(a<=b)
local a = "abcd"; local b = "abce"; print(a<=b)

local a = "abc"; local b = "abcd"; print(not(a<=b))
local a = "abcd"; local b = "abc"; print(not(a<=b))
local a = "abc"; local b = "abc"; print(not(a<=b))
local a = "abcd"; local b = "abce"; print(not(a<=b))

print('-- test 8 --')
print((pcall(function() print(function(x, y) return x end <= function(x,y) return y end) end)))
print((pcall(function() print(not(function(x, y) return x end <= function(x,y) return y end)) end)))

debug.setmetatable(function(x, y) end, {
	__le = function(a,b)
		print("func_mt")
		return 123
	end
})

print(function(x, y) return x end <= function(x,y) return y end)
print(not(function(x, y) return x end <= function(x,y) return y end))

print('-- test 9 --')

print((pcall(function() local a = 123; local b = "456"; print(a<=b); end)))
print((pcall(function() local a = 123; local b = "456"; print(not(a<=b)); end)))

debug.setmetatable(1, {
	__le = function(a,b)
		print("should never reach here")
	end
})

print((pcall(function() local a = 123; local b = "456"; print(a<=b); end)))
print((pcall(function() local a = 123; local b = "456"; print(not(a<=b)); end)))

local a = 1; local b = 2; print(a<=b)
local a = 1; local b = 2; print(not(a<=b))

local a = 2; local b = 1; print(a<=b)
local a = 2; local b = 1; print(not(a<=b))

local a = 1; local b = 1; print(a<=b)
local a = 1; local b = 1; print(not(a<=b))

print('--- test 10 ---')

t1 = { name = "t1" }
t2 = { name = "t2" }

f1 = function(a,b,c)
	print("f1", a.name, b.name, c)
	return {}
end

setmetatable(t1, {
	__le = f1
})

setmetatable(t2, {
	__le = f1
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 11 ---')

f2 = function(a,b,c)
	print("f2", a.name, b.name, c)
	return
end

setmetatable(t1, {
	__le = f2
})

setmetatable(t2, {
	__le = f2
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 12 ---')

t3 = { name = "t3" }
t4 = { name = "t4" }

setmetatable(t1, {
	__le = t3
})

setmetatable(t2, {
	__le = t4
})


f3 = function(a,b,c,d)
	print("f3", a.name,b.name,c.name,d)
	return 12
end

setmetatable(t3, {
	__eq = f3,
	__call = function(a,b,c,d)
		print("t3_0", a.name,b.name,c.name,d)
		return 321
	end
})

setmetatable(t4, {
	__eq = f3
})
print(t1 <= t1)
print(not(t1 <= t1))
print((pcall(function() print(t1 <= t2) end)))
print((pcall(function() print(not(t1 <= t2)) end)))

print('--- test 13 ---')

setmetatable(t3, {
	__call = function(a,b,c,d)
		print("t3_1", a.name,b.name,c.name,d)
		return 321
	end
})

setmetatable(t1, {
	__le = t3
})

setmetatable(t2, {
	__le = t3
})
print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 14 ---')

setmetatable(t3, {
	__call = function(a,b,c,d)
		print("t3_2", a.name,b.name,c.name,d)
		return
	end
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 15 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn", a,b.name,c.name,d)
		return "abc"
	end
})

setmetatable(t1, {
	__le = 1234
})

setmetatable(t2, {
	__le = 1234
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 16 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_2", a,b.name,c.name,d)
		return false
	end
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 17 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_3", a,b.name,c.name,d)
		return "abc"
	end
})

setmetatable(t1, {
	__le = 1234
})

setmetatable(t2, {
	__le = "1234"
})

print(t1 <= t1)
print(not(t1 <= t1))
print((pcall(function() print(t1 <= t2) end)))
print((pcall(function() print(not(t1 <= t2)) end)))

print('--- test 18 ---')

setmetatable(t1, {
	__le = 0.0/0
})

setmetatable(t2, {
	__le = 0.0/0
})

print((pcall(function() print(t1 <= t1) end)))
print((pcall(function() print(not(t1 <= t1)) end)))
print((pcall(function() print(t1 <= t2) end)))
print((pcall(function() print(not(t1 <= t2)) end)))

print('--- test 19 ---')

setmetatable(t1, {
	__le = 1 * 0
})

setmetatable(t2, {
	__le = -1 * 0
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 20 ---')

debug.setmetatable(123, {
	__call = function(a,b,c,d)
		print("tn_4", a,b.name,c.name,d)
		return 
	end
})

print(t1 <= t1)
print(not(t1 <= t1))
print(t1 <= t2)
print(not(t1 <= t2))

print('--- test 21 ---')

debug.setmetatable(123, {})

print((pcall(function() print(t1 <= t1) end)))
print((pcall(function() print(not(t1 <= t1)) end)))
print((pcall(function() print(t1 <= t2) end)))
print((pcall(function() print(not(t1 <= t2)) end)))

print('test end')

