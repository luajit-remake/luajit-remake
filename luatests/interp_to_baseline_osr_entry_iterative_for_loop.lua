iter = function(state, var)
	var = var + 1
	if var == state then
		var = nil
	end
	return var
end

function f(n)
	local sum = 0
	for i in iter, n, 0 do
		sum = sum + i
	end
	print(sum)
end

f(50000)

