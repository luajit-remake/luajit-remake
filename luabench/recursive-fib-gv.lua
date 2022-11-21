-- Public domain
-- Same as recursive-fib-uv, but instead of using upvalue, 
-- this version uses direct global variable lookup
f = function(n)
	if (n <= 2) then
		return 1
	end 
	return f(n-1) + f(n-2)
end

local n = tonumber(arg[1]) or 10
local result = f(n)
print(result)

