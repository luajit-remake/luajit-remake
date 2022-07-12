t = {}
t.a = 1
t.b = 'x'
t.c = 1.23
t[1] = 1
t[2] = 3
t[3] = 5.6
print('-- test 1 --')
local hide = next
for key, val in hide, t, nil do
	print(key, val)
end
print('-- test 2 --')
t[0] = 'z'
t[2.5] = 234
t[4] = 7
for key, val in hide, t, nil do
	print(key, val)
end
print('-- test 3 --')
t[1000000] = 8.9
for i = 5, 20 do
	t[i] = i + 100
end
for key, val in hide, t, nil do
	print(key, val)
end


