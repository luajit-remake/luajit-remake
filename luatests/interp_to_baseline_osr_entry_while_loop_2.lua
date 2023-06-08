function f(n)
	local i = 1
	local sum = 0
	while (i < n) do
		local t = i
		local g = function()
			return t
		end
		sum = sum + g()
		i = i + 1
	end
	print(sum)
end

f(50000)

