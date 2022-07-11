f = function(state, var)
	state = state + 1
	var = var + 1
	if var == 5 then
		var = nil
	end
	return var, state
end

get_exp_list = function()
	print('for init')
	return f, 0, 0
end

for a, b, c, d in get_exp_list() do
	print(a, b, c, d)
end

