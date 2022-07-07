local f = function()
	return 1,2,3
end

local t = { f() }
print(t[0], t[1], t[2], t[3], t[4])

local t2 = { a = 123, b = 456, f() }
print(t2['a'], t2['b'], t[0], t[1], t[2], t[3], t[4])

