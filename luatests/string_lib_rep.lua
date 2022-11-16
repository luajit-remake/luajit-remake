print(type(string.rep("", 10)))
print(string.rep("", 10))
print(type(string.rep("a", 0)))
print(string.rep("a", 0))
print(type(string.rep("ab", 0)))
print(string.rep("ab", 0))
print(string.rep("a", 3))
print(string.rep("abc", 3))
print(string.rep("a", 10))
print(string.rep("a", 3456))
print(string.rep("a", 9876))
print(string.rep("ab", 10))
print(string.rep("ab", 3456))
print(string.rep("abc", 3456))

s = "aba"
s = s .. "c" .. s
print(string.rep(s, 101))
print(string.rep(s, 333))
s = s .. "d" .. s
print(string.rep(s, 100))
print(string.rep(s, 111))
s = s .. "e" .. s
print(string.rep(s, 19))
print(string.rep(s, 23))
s = s .. "f" .. s
print(string.rep(s, 27))
print(string.rep(s, 42))
s = s .. "g" .. s
print(string.rep(s, 20))
print(string.rep(s, 31))
s = s .. "h" .. s
print(string.rep(s, 13))
print(string.rep(s, 22))
s = s .. "i" .. s
print(string.rep(s, 7))
print(string.rep(s, 10))
s = s .. "j" .. s
print(string.rep(s, 4))
print(string.rep(s, 5))
s = s .. "k" .. s
print(string.rep(s, 3))
s = s .. "l" .. s
print(string.rep(s, 2))

