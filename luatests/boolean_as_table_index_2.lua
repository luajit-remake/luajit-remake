local t = {}
t['a'] = 1
t['b'] = 2
t[false] = 3
t['c'] = 4
t[true] = 5
t['d'] = 6
t[1] = 2
t[2] = 3
t[0] = 4

hide = pairs

for k, v in hide(t) do
	print(k, v)
end

