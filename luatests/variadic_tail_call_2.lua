local cnt = 0 
local cnt2 = 0
local cnt3 = 0
local cnt4 = 0
local cnt5 = 0
local cnt6 = 0
local cnt7 = 0
local cnt8 = 0
local cnt9 = 0
local cnt10 = 0
local cnt11 = 0
local cnt12 = 0
local cnt13 = 0

function f()
	cnt = cnt + 1
	local rem = cnt % 5
	if rem == 0 then
		return
	elseif rem == 1 then
		return 1
	elseif rem == 2 then
		return 1, 2
	elseif rem == 3 then
		return 1, 2, 3
	else 
		return 1, 2, 3, 4
	end
end

function g(n, x, ...)
	if n == 0 then
		return
	end
	if x ~= nil then
		cnt2 = cnt2 + x
	else 
		cnt3 = cnt3 + 1
	end
	local a, b, c, d, e = ...
	if a ~= nil then
		cnt4 = cnt4 + a
	else
		cnt5 = cnt5 + 1
	end
	if b ~= nil then
		cnt6 = cnt6 + b
	else
		cnt7 = cnt7 + 1
	end
	if c ~= nil then
		cnt8 = cnt8 + c
	else
		cnt9 = cnt9 + 1
	end
	if d ~= nil then
		cnt10 = cnt10 + d
	else
		cnt11 = cnt11 + 1
	end
	if e ~= nil then
		cnt12 = cnt12 + e
	else
		cnt13 = cnt13 + 1
	end
	return g(n - 1, f())
end

print(g(100000))
print(cnt, cnt2, cnt3, cnt4, cnt5, cnt6, cnt7, cnt8, cnt9, cnt10, cnt11, cnt12, cnt13)
 
