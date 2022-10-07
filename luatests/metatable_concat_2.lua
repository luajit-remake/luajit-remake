function concat(a, b, c, d, e, f)
	local res = a .. b .. c .. d .. e .. f 
	print(res)
	print()
end

debug.setmetatable(nil, {
	__concat =  function(lhs, rhs)
		print('enter concat nil', lhs, rhs)
		lhs_s = lhs
		if lhs == nil then 
			lhs_s = "nil"
		elseif lhs == false then
			lhs_s = "false"
		elseif lhs == true then
			lhs_s = "true"
		end
		rhs_s = rhs
		if rhs == nil then 
			rhs_s = "nil"
		elseif rhs == false then
			rhs_s = "false"
		elseif rhs == true then
			rhs_s = "true"
		end
		return lhs_s .. rhs_s
	end
})

debug.setmetatable(false, {
	__concat =  function(lhs, rhs)
		print('enter concat bool', lhs, rhs)
		lhs_s = lhs
		if lhs == nil then 
			lhs_s = "nil"
		elseif lhs == false then
			lhs_s = "false"
		elseif lhs == true then
			lhs_s = "true"
		end
		rhs_s = rhs
		if rhs == nil then 
			rhs_s = "nil"
		elseif rhs == false then
			rhs_s = "false"
		elseif rhs == true then
			rhs_s = "true"
		end
		return lhs_s .. rhs_s
	end
})

concat(false, "789", 345.6, true, nil, 234)
concat(123, true, false, "a", "b", nil)
concat(23.4, true, false, "c", nil, true)
concat(23.4, true, nil, "d", true, nil)
concat(23.4, nil, nil, "e", nil, nil)
concat(23.4, nil, nil, "e", false, false)

