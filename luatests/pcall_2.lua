-- test all sorts of edge cases to pcall

function test()
	pcall()
	print("should never reach here")
end

function f()
	print('enter f')
	return 123
end

print(xpcall(test, f))

print(pcall(nil))
print(pcall(nil, 1, 2))
print(pcall(123, 456))
print(pcall({}, 456))

function g(a, b, c)
	print('enter g', a, b, c)
	return 321, 654
end

print(pcall(g))
print(pcall(g, 1))
print(pcall(g, 1, 2, 3))
print(pcall(g, 1, 2, 3, 4, 5))

function g2(a, b, c, ...)
	local x = { ... }
	print('enter g2', a, b, c, #x, x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7])
	return 233, 124
end

print(pcall(g2))
print(pcall(g2, 1))
print(pcall(g2, 1, 2, 3))
print(pcall(g2, 1, 2, 3, 4, 5))
print(pcall(g2, 1, 2, 3, 4, 5, 6, 7))
print(pcall(g2, 1, 2, 3, 4, 5, 6, 7, 8, 9))

