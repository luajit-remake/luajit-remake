f = function(n)
	if (n <= 2) then
		return 1
	end 
	return f(n-1) + f(n-2)
end

local result = f(15)
print(result)

