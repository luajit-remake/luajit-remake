local h = { name = "h" }
local eat2 = function(a, b, ...)
	return ...
end

local g = function(...)
	local arg = {...}
	local len = #arg
	for i = 1, len do
		io.write(arg[i].name .. " ")
	end
	io.write("\n")
	if (#arg == 1) then
		return 123, 456
	end
	return h(eat2(...))
end

local f = function(...)
	local arg = {...}
	local len = #arg
	for i = 1, len do
		io.write(arg[i].name .. " ")
	end
	io.write("\n")
	if (#arg == 10) then
		getmetatable(h).__call = g
	end
	return h(...)
end

local mt = { __call = f }
setmetatable(h, mt)

print(h({ name = 'x' }))

