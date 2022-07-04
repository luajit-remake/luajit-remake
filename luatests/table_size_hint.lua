local a = 1
local b = 2
local c = 3
t = { [a] = 3, [b] = 4, [c] = 5 }
t2 = { x = a, y = b, z = c }
t3 = { a, a, a, a }
print(t[1], t[2], t[0], t2['x'], t2['y'], t2['a'], t3[1], t3[2], t3[0])

