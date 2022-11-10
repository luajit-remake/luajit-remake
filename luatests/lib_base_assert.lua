print(assert(true, 1, 2, 3, 4))
print(assert(123, 456, 789))
print(assert("aaa", "bbb", "ccc"))
ff = function() end
x1, x2, x3 = assert(ff, "f", "fff")
if (not (x1 == ff and x2 == "f" and x3 == "fff")) then
	print("bad")
end

test1 = function()
	assert()
end
print((pcall(test1)))

test2 = function(b)
	assert(b, "test err msg")
end
print((pcall(test2, true)))
print((pcall(test2, false)))
print((pcall(test3, nil)))
print((pcall(test2, 123)))
print((pcall(test2, ff)))
print((pcall(test2, "aaa")))

test3 = function(b)
	assert(b)
end
print((pcall(test3, true)))
print((pcall(test3, false)))
print((pcall(test3, nil)))
print((pcall(test3, 123)))
print((pcall(test3, ff)))
print((pcall(test3, "aaa")))

