f = function(a)
	h = function()
		return a
	end
	g = function()
		a = a + 1
		return a
	end
	return a
end

print('------')
print(f(1))
print(h())
print(g())
print(h())
print(g())
print(h())

print('------')
h1 = h
g1 = g
print(f(10))
print(h())
print(g())
print(h())
print(g())
print(h())

print('------')
print(h1())
print(g1())
print(h1())
print(g1())
print(h1())

print('------')
print(h())
print(g())
print(h())
print(g())
print(h())
