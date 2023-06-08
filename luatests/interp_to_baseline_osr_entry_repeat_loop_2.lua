function f(n)
	local i = 1
	local sum = 0
	repeat
		local t = i
		local g = function()
			return t
		end
		sum = sum + g()
		i = i + 1
	until i >= n
	print(sum)
end

f(50000)

