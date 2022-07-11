-- poison the global variable 'pair' to not return 'next
-- LuaJIT should still generate ISNEXT/ITERN bytecode
-- so this test tests the case that ISNEXT works correctly under validation failure case

oldPairs = pairs

f = function(s, v)
	print(s, v)
	s = s + 1
	v = v + 1
	if v == 205 then
		v = nil
	end
	return v, 1, 2
end

function iter(t)
	for v1, v2 in pairs(t) do
		print(v1, v2)
		pairs = oldPairs
	end
end



t = { x = 1, [2] = 3}

iter(t)

pairs = function(x)
	print('enter pairs')
	return f, 100, 200
end

iter(t)

iter(t)

pairs = function(x)
	print('enter pairs')
	return f, 100, 200
end

iter(t)

