-- poison the global variable 'next'
-- this should not affect the result because 'pairs' always return the true 'next'
next = 0

print(next)

t = {}
t.a = 1
t.b = 'x'
t.c = 1.23
t[1] = 1
t[2] = 3
t[3] = 5.6
print('-- test 1 --')
for key, val in pairs(t) do
	print(key, val)
end
print('-- test 2 --')
t[0] = 'z'
t[2.5] = 234
t[4] = 7
for key, val in pairs(t) do
	print(key, val)
end
print('-- test 3 --')
t[1000000] = 8.9
for i = 5, 20 do
	t[i] = i + 100
end
for key, val in pairs(t) do
	print(key, val)
end


