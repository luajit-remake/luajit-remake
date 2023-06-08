function f(n)
	local i = 1
	local sum = 0
	repeat 
		sum = sum + i
		i = i + 1
	until i >= n
	print(sum)
end

f(50000)
 
