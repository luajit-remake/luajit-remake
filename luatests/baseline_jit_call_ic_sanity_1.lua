function f(g, x)
	local result = g(x)
	return result
end

-- must be the only 3 functions with 1 arg.. we use this to identify it in unit test 
function add1(x) return x + 1 end
function add2(x) return x + 2 end
function add3(x) return x + 3 end

print(f(add1, 123))
print(f(add2, 234))
print(f(add3, 345))
print(f(add1, 456))
print(f(add2, 567))
print(f(add3, 678))
print(f(add1, 789))
print(f(add2, 890))
print(f(add3, 901))

