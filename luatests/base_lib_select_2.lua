print(select(-1, 1,2,3,4,5))
print(select(-2, 1,2,3,4,5))
print(select(-3, 1,2,3,4,5))
print(select(-4, 1,2,3,4,5))
print(select(-5, 1,2,3,4,5))

print((pcall(function() select(-6, 1,2,3,4,5) end)))
print((pcall(function() select(0) end)))
print((pcall(function() select(0,1) end)))
print((pcall(function() select(-10000,1,2,3) end)))

