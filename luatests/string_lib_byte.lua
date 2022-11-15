local a, b = ("foo"):byte(1)
print(type(a))
print(a, b)

local c, d = ("foo"):byte(2, 3)
print(type(c))
print(c, d)

print(string.byte("abcdefghijklmn", 1, 5))
print(string.byte("abcdefghijklmn", 1, -5))
print(string.byte("abcdefghijklmn", -10, -5))
print(string.byte("abcdefghijklmn", 5, 1))
print(string.byte("abcdefghijklmn", 1, 1))
print(string.byte("abcdefghijklmn", 15, 18))
print(string.byte("abcdefghijklmn", -100, -90))
print(string.byte("abcdefghijklmn", 1000, 10000))
print(string.byte("abcdefghijklmn", -10, 10))

