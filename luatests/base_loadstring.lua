print((loadstring("qwerty")))
print((loadstring("123")))
print((loadstring(123.4)))
print((pcall(function() loadstring() end)))
print((pcall(function() loadstring({}) end)))

local res = loadstring("print('in loaded string!')")
print(type(res))
res()

local t1 = 123;
g1 = 234;

print('main', t1, g1)
local res = loadstring("print('loaded', t1,g1); t1=345; g1=456; print('loaded', t1,g1)")
res()
print('main', t1, g1)

local res = loadstring("print('loaded',...)")
res()
res('w')
res(1,2)
res('x','y','z')

