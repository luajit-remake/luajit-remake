print('-- test 1 --')
t = { [0] = "a", [1] = "b", [2] = "c", [3] = "d", [5] = "e", [-1] = "f", xxx = "g" }
for k,v in ipairs(t) do
	print(k,v)
end

print('-- test 2 --')
for k,v in ipairs({}) do
	print(k,v)
end

print('-- test 3 --')
t = { [2] = "c", [4] = "d", [-1] = "e", xxx = "f" }
for k,v in ipairs({}) do
	print(k,v)
end

print('-- test 4 --')
t = { [2] = "c", [4] = "d", [-1] = "e", xxx = "f" }
for k,v in ipairs({}) do
	print(k,v)
end

-- ipairs ignores metatable
print('-- test 5 --')
t = { }
setmetatable(t, {
	__index = function(t, i)
		print("should never reach here")
	end
})
for k,v in ipairs(t) do
	print(k,v)
end

print('-- test 6 --')
t[1] = 233
for k,v in ipairs(t) do
	print(k,v)
end

print('-- test 7 --')
t[1] = nil
for k,v in ipairs(t) do
	print(k,v)
end

