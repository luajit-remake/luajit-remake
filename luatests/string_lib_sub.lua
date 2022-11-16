local a, b = ("foo"):sub(1)
print(type(a))
print(a, b)

local c, d = ("foo"):sub(2, 3)
print(type(c))
print(c, d)

local e, f = ("foo"):sub(4, 3)
print(type(e))
print(e, f)

print(string.sub("abcdefghijklmn", 1, 5))
print(string.sub("abcdefghijklmn", 1, -5))
print(string.sub("abcdefghijklmn", -10, -5))
print(string.sub("abcdefghijklmn", 5, 1))
print(string.sub("abcdefghijklmn", 1, 1))
print(string.sub("abcdefghijklmn", 15, 18))
print(string.sub("abcdefghijklmn", -100, -90))
print(string.sub("abcdefghijklmn", 1000, 10000))
print(string.sub("abcdefghijklmn", -10, 10))
 
