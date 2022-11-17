function f(t1, t2)
	local x = t1.v
	x = t2.v or x
	return x
end

print(f({ v = 1 }, { v = 2 }))
print(f({ v = 1 }, { v = true }))
print(f({ v = 1 }, { v = false }))
print(f({ v = 1 }, { v = nil }))
print(f({ v = 1 }, { v = "xx" }))

