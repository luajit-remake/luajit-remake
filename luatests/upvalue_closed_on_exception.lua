f = function()
	local x = 1
	g = function()
		x = x + 1
		return x
	end
	error("x")
end

pcall(f)
local g1 = g

pcall(f)
local g2 = g

print(g1())
print(g2())
print(g1())
print(g2())

