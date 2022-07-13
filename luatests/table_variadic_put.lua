local f = function()
	return 1,2,3
end

local f2 = function()
	return 1,2,3,4
end

local f3 = function()
	return 
end

local t = { f() }
print(t[0], t[1], t[2], t[3], t[4])

local t2 = { a = 123, b = 456, f() }
print(t2['a'], t2['b'], t[0], t[1], t[2], t[3], t[4])

local b = 'ccc'
local c = 'ddd'
local t3 = { a = 4, b, c, f2() }
print(t3['a'], t3['b'], t3[0], t3[1], t3[2], t3[3], t3[4], t3[5], t3[6], t3[7])


local t4 = { c, b, f3() }
print(t4['a'], t3['b'], t4[0], t4[1], t4[2], t4[3])

local t5 = { f3() }
print(t5['a'], t5['b'], t5[0], t5[1])
print(next(t5))

local t6 = { [10000] = 'a', f2() }
print(t6['a'], t6['b'], t6[0], t6[1], t6[2], t6[3], t6[4], t6[5], t6[10000], t6[10001], t6[10002], t6[10003], t6[10004], t6[10005])

