local h = { name = "h" }
local f = function(...)
	local arg = {...}
	local len = #arg
	for i = 1, len do
		io.write(arg[i].name .. " ")
	end
	io.write("\n")
	if (#arg == 10) then
		return 123, 456
	end
	return h(...)
end

local mt = { __call = f }
setmetatable(h, mt)

print(h({ name = 'x' }))
 
