f = function(a)
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	a = a + 1
	print(a)
end

g = function(b, func)
	if b then
		for i = 1,20 do
			func(i * 3 - 2)
			func(i * 3 - 1)
			func(i * 3)
		end
	end
	
	-- make sure g will be in baseline JIT mode in next run
	local x = 1
	for i = 1,50000 do
		x = x + 1
	end
	print(x)
end

g(false, nil)
g(true, f)


