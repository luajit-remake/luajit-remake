function f(g, x)
	local result = g(x)
	return result
end

function fnFactory1(x, dummy)
	local sum = 0
	return function(t)
		sum = sum + t + x
		return sum
	end
end

function fnFactory2(x, dummy)
	local sum = 0
	return function(t)
		sum = sum + t * 2 + x * 3
		return sum
	end
end

function fnFactory3(x, dummy)
	local sum = 0
	return function(t)
		sum = sum + t * 3 + x * 4
		return sum
	end
end

list = {}
list[1] = fnFactory1(12)
list[2] = fnFactory1(23)
list[3] = fnFactory2(34)
list[4] = fnFactory2(45)
list[5] = fnFactory1(56)
list[6] = fnFactory2(67)
list[7] = fnFactory3(78)
list[8] = fnFactory1(89)
list[9] = fnFactory2(90)
list[10] = fnFactory3(101)
list[11] = fnFactory3(210)
list[12] = fnFactory2(321)
list[13] = fnFactory2(432)
list[14] = fnFactory1(543)
list[15] = fnFactory1(654)

for i = 1, 15 do
	print(f(list[i], i))
	print(f(list[i], i))
end


