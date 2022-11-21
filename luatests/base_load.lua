function f1() return nil end

print('-- test 1 --')
local res = load(f1)
print(type(res))
print('execution result', res())

print('-- test 2 --')
function f2() return "" end
local res = load(f2)
print(type(res))
print('execution result', res())

print('-- test 3 --')
function f3() return end
local res = load(f3)
print(type(res))
print('execution result', res())

print('-- test 4 --')
function get_f4()
	local x = true
	return function () if x then x = false ; return "print('in load'); return 123" else return "" end end
end
local res = load(get_f4())
print(type(res))
print('execution result', res())

print('-- test 5 --')
function get_f5()
	local x = true
	return function () if x then x = false ; return "print('in load'); return 123, 456" else return "" end end
end
local res = load(get_f5())
print(type(res))
print('execution result', res())

print('-- test 6 --')
function get_f6()
	local tt = { [1] = "print('in", [2] = " load'); return 1", [3] = "234, 567", cur = 1 }
	return function () local idx = tt.cur; tt.cur = tt.cur + 1; return tt[idx] end
end

local res = load(get_f6())
print(type(res))
print('execution result', res())

print('-- test 7 --')
function get_f7()
	local x = true
	return function () if x then x = false ; return {} else return "" end end
end
print((load(get_f7())))

print('-- end of test --')
