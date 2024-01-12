f = function(a)
	repeat
		a = a + 1
	until false
end

g = function(a, b)
	local c = b + b
	-- the IC of this call will point to the function 'f', 
	-- which is a dead loop, so 'c' is dead in DFG 
	-- but this is a speculation so it can fail at runtime
	-- so 'c' is still needed for OSR exit!
	f(a)
	return c
end

pcall(g, {}, 1)

