metatable = {
	__add = function(a, b)
		return {
			name = a.name + b.name
		}
	end
}

make = function(k)
	return setmetatable({
		name = k
	}, metatable)
end

s = make(0)
for i = 1, 100 do
	s = s + make(i)
end
print(s.name)

