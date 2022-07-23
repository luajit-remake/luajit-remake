print('--- part 1 ---')
-- test error case (no param passed)
success = pcall(function() getmetatable() end)
print(success)	

-- test error case (no param passed)
success = pcall(function() setmetatable() end)
print(success)	

-- test error case (metatable is not a table)
success = pcall(function() setmetatable({}, 123) end)
print(success)	

success = pcall(function() setmetatable({}, false) end)
print(success)	

success = pcall(function() setmetatable({}, true) end)
print(success)	

success = pcall(function() setmetatable({}, "ab") end)
print(success)	

success = pcall(function() setmetatable({}, function() end) end)
print(success)	

print('--- part 2 ---')
print(getmetatable(nil))
print(debug.getmetatable(nil))
print(getmetatable(false))
print(debug.getmetatable(false))
print(getmetatable(true))
print(debug.getmetatable(true))
print(getmetatable(123.4))
print(debug.getmetatable(123.4))
print(getmetatable(function() end))
print(debug.getmetatable(function() end))
print(getmetatable({}))
print(debug.getmetatable({}))

print('--- part 3 ---')
t = { name = "t" }
print(getmetatable(t))
print(debug.getmetatable(t))
mt_t = { name = "mt_t" }
print(setmetatable(t, mt_t).name)
print(getmetatable(t).name)
print(debug.getmetatable(t).name)

-- when a __metatable field exists in the metatable, getmetatable should return the value associated with __metatable field, while debug.getmetatable is unaffected
mt_t.__metatable = "abcdefg"
print(getmetatable(t))
print(debug.getmetatable(t).name)

-- when a __metatable field exists, setmetatable should fail, but debug.setmetatable is unaffected
success = pcall(function() setmetatable(t, {}) end)
print(success)

print(debug.setmetatable(t, { name = "overwritten" }))

print(getmetatable(t).name)
print(debug.getmetatable(t).name)

print('--- part 4 ---')
-- setmetatable can only set the metatable of tables, not anything else
success = pcall(function() setmetatable(nil, {}) end)
print(success)
success = pcall(function() setmetatable(false, {}) end)
print(success)
success = pcall(function() setmetatable(true, {}) end)
print(success)
success = pcall(function() setmetatable(123.4, {}) end)
print(success)
success = pcall(function() setmetatable("abcd", {}) end)
print(success)
success = pcall(function() setmetatable(function() end, {}) end)
print(success)

print('--- part 5 ---')
-- but debug.setmetatable should be able to change anything
print(debug.setmetatable(nil, { name = "nil_mt" }))
print(debug.setmetatable(false, { name = "bool_mt" }))
print(debug.setmetatable(123.4, { name = "number_mt" }))
debug.setmetatable(function() end, { name = "func_mt" })
debug.setmetatable("abc", { name = "string_mt" })

-- and getmetatable should be able to get them
print(getmetatable(nil).name)
print(debug.getmetatable(nil).name)
print(getmetatable(false).name)
print(debug.getmetatable(false).name)
print(getmetatable(true).name)
print(debug.getmetatable(true).name)
print(getmetatable(234.5).name)
print(debug.getmetatable(234.5).name)
print(getmetatable(function(a) return a end).name)
print(debug.getmetatable(function(a) return a end).name)
print(getmetatable("abcde").name)
print(debug.getmetatable("abcde").name)

print('--- part 6 ---')
-- now test an edge case where the per-type metatable becomes protected
getmetatable(nil).__metatable = "protect!"
print(getmetatable(nil))
print(debug.getmetatable(nil).name)
print(debug.setmetatable(nil, { name = "nil_mt_2" }))
print(debug.getmetatable(nil).name)

print('--- part 7 ---')
-- test removing metatable
print(debug.setmetatable(nil, nil))
print(debug.getmetatable(nil))

getmetatable(t).__metatable = "protect_t"
success = pcall(function() setmetatable(t, nil) end)
print(success)

print(debug.setmetatable(t, nil))
print(getmetatable(t))
print(debug.getmetatable(t))

print(setmetatable(t, { name = "mt_t_2" }).name)
print(getmetatable(t).name)
print(setmetatable(t, nil).name)
print(getmetatable(t))
print(debug.getmetatable(t))

