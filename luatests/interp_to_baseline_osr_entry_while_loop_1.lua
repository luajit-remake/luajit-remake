function f(n)
	local i = 1
	local sum = 0
	while (i < n) do
		sum = sum + i
		i = i + 1
	end
	print(sum)
end

f(50000)

