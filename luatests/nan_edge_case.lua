lt = function(a, b)
	if a < b then
		print('1')
	else
		print('0')
	end
end
le = function(a, b)
	if a <= b then
		print('1')
	else
		print('0')
	end
end
gt = function(a, b)
	if a > b then
		print('1')
	else
		print('0')
	end
end
ge = function(a, b)
	if a >= b then
		print('1')
	else
		print('0')
	end
end
eq = function(a, b)
	if a == b then
		print('1')
	else
		print('0')
	end
end
ne = function(a, b)
	if a ~= b then
		print('1')
	else
		print('0')
	end
end

lt(0, 1)
lt(1, 0)
lt(0, 0)
lt(0.0/0, 1)
lt(1, 0.0/0)
lt(0.0/0, 0.0/0)

le(0, 1)
le(1, 0)
le(0, 0)
le(0.0/0, 1)
le(1, 0.0/0)
le(0.0/0, 0.0/0)

gt(0, 1)
gt(1, 0)
gt(0, 0)
gt(0.0/0, 1)
gt(1, 0.0/0)
gt(0.0/0, 0.0/0)

ge(0, 1)
ge(1, 0)
ge(0, 0)
ge(0.0/0, 1)
ge(1, 0.0/0)
ge(0.0/0, 0.0/0)

eq(0, 1)
eq(1, 0)
eq(0, 0)
eq(0.0/0, 1)
eq(1, 0.0/0)
eq(0.0/0, 0.0/0)

ne(0, 1)
ne(1, 0)
ne(0, 0)
ne(0.0/0, 1)
ne(1, 0.0/0)
ne(0.0/0, 0.0/0)

