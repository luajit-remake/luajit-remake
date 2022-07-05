fn = function(x, y)
	local a = 1
	local b = 2
	return function(c, d)
		local e = 3
		return function(f)
			x = x + 10000
			a = a + 1000
			c = c + 100
			e = e + 10
			print(x, y, a, b, c, d, e, f)
		end
	end
end

f1 = fn(123, 456)
f2 = f1(78, 90)
f4 = fn(765, 43)
f3 = f1(12, 34)
f5 = f4(12, 963)
f6 = f4(233, 852)
f2(54321)
f5(4356)
f2(65432)
f6(7531)
f3(987)
f6(9999)
f3(654)
f6(987)
f2(432)
f5(3232)
f2(321)
f3(16)
f7 = f1(9012, 234)
f7(88)

