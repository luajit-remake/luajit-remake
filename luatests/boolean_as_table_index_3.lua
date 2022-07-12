function f(t, i, v)
	t[i] = v
end

function g(t, i)
	return t[i]
end

t = {}

f(t, false, 1)
f(t, 'a', 2)
f(t, true, 3)
f(t, 1, 4)

print(g(t, false), g(t, true), g(t, 'a'), g(t, 0), g(t, 1), g(t, 2))

f(t, false, 5)
f(t, 'a', 6)
f(t, true, 7)
f(t, 1, 8)

print(g(t, false), g(t, true), g(t, 'a'), g(t, 0), g(t, 1), g(t, 2))

f(t, false, nil)
f(t, 'a', nil)
f(t, true, nil)
f(t, 1, nil)

print(g(t, false), g(t, true), g(t, 'a'), g(t, 0), g(t, 1), g(t, 2))

