print(string.char(65,66,67,68,69))
x = string.char()
print(type(x))
print(x)

print(string.char(' 0x41 ', ' 66 ', '67', 68, '69  '))

print((pcall(function() string.char(-1) end)))
print((pcall(function() string.char(256) end)))
print((pcall(function() string.char('a') end)))
print((pcall(function() string.char({}) end)))

