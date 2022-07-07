local function f1(a, b, c)
	local x = a and b or c
	print(x)
end

local function f2(a, b, c)
	local x = (not a) and b or c
	print(x)
end

local function f3(a, b)
	local x = a and b
	print(x)
end

print('f1')
f1(nil, 1, 2)
f1(false, 3, 4)
f1(true, 5, 6)
f1(233, 7, 8)
f1({ x = 2333 }, 9, 10)
f1(0.0/0, 11, 12)
f1(1.0/0, 13, 14)
f1("a", 15, 16)

print('f2')
f2(nil, 17, 18)
f2(false, 19, 20)
f2(true, 21, 22)
f2(23333, 23, 24)
f2({ x = 2333 }, 25, 26)
f2(0.0/0, 27, 28)
f2(1.0/0, 29, 30)
f2("a", 31, 32)

print('f3')
f3(nil, 33)
f3(false, 34)
f3(true, 35)
f3(233333, 36)
f3({ x = 2333 }, 37)
f3(0.0/0, 38)
f3(1.0/0, 39)
f3("a", 40)
