print(rawequal(0, 0))
print(rawequal(0, -0))
print(rawequal(-0, 0))
print(rawequal(-0, -0))
print(rawequal(0/0.0, 0/0.0))
print(rawequal(1/0.0, 1/0.0))
print(rawequal(-1/0.0, -1/0.0))
print(rawequal(1/0.0, -1/0.0))
print(rawequal(0/0.0, 0))
print(rawequal(0/0.0, 1/0.0))
print(rawequal(12, 12))
print(rawequal(10, 12))
print(rawequal("123", 123))
print(rawequal(123, 123))
t1 = {}
t2 = {}
print(rawequal(t1, t1))
print(rawequal(t1, t2))
print(rawequal(t2, t1))
print(rawequal(t2, t2))

f1 = function() print('a') end
f2 = function() print('b') end
print(rawequal(f1, f1))
print(rawequal(f1, f2))
print(rawequal(f2, f1))
print(rawequal(f2, f2))

print(rawequal(f1, t1))
print(rawequal(f1, t2))
print(rawequal(f2, t1))
print(rawequal(f2, t2))

mt = {
	__eq = function()
		return true
	end
}
setmetatable(t1, mt)
setmetatable(t2, mt)
print(rawequal(t1, t1))
print(rawequal(t1, t2))
print(rawequal(t2, t1))
print(rawequal(t2, t2))
print(t1 == t1)
print(t1 == t2)
print(t2 == t1)
print(t2 == t2)

