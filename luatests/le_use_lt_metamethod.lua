local lt = function(a, b)
	print(string.format("in lt metamethod, lhs = %s, rhs = %s", a.name, b.name))
	return true
end
local lt2 = function()
	print(string.format("in lt2 metamethod, lhs = %s, rhs = %s", a.name, b.name))
	return true
end
local le = function()
	print(string.format("in le metamethod, lhs = %s, rhs = %s", a.name, b.name))
	return true
end
local le2 = function()
	print(string.format("in le2 metamethod, lhs = %s, rhs = %s", a.name, b.name))
	return true
end
local ltchoice = { [1] = lt, [2] = lt2, [3] = nil }
local ltchoiceName = { [1] = "lt", [2] = "lt2", [3] = "nil" }
local lechoice = { [1] = le, [2] = le2, [3] = nil }
local lechoiceName = { [1] = "le", [2] = "le2", [3] = "nil" }

local k = 0
local mtList = {}
for i = 1,3 do
	for j = 1,3 do
		local mt = {}
		mt.ltName = ltchoiceName[i]
		if ltchoice[i] ~= nil then
			mt.__lt = ltchoice[i]
		end
		mt.leName = lechoiceName[j]
		if lechoice[j] ~= nil then
			mt.__le = lechoice[j]
		end
		k = k + 1
		mtList[k] = mt
	end
end
k = k + 1
mtList[k] = nil

function run_test_impl(mt1, mt2, op, fn)
	local mt1_ltName = "(no mt)"
	local mt1_leName = "(no mt)"
	if mt1 ~= nil then
		mt1_ltName = mt1.ltName
		mt1_leName = mt1.leName
	end
	local mt2_ltName = "(no mt)"
	local mt2_leName = "(no mt)"
	if mt2 ~= nil then
		mt2_ltName = mt2.ltName
		mt2_leName = mt2.leName
	end
	print(string.format("Executing test: a.lt = %s, a.le = %s, b.lt = %s, b.le = %s, op = %s", mt1_ltName, mt1_leName, mt2_ltName, mt2_leName, op))
	print((pcall(fn, op)))
end

function run_test(mt1, mt2)
	local a = { name = "a" }
	setmetatable(a, mt1)
	local b = { name = "b" }
	setmetatable(b, mt2)
	
	run_test_impl(mt1, mt2, "a<b", function(op) print(op, a<b) end)
	run_test_impl(mt1, mt2, "a<=b", function(op) print(op, a<=b) end)
	run_test_impl(mt1, mt2, "a>b", function(op) print(op, a>b) end)
	run_test_impl(mt1, mt2, "a>=b", function(op) print(op, a>=b) end)
	run_test_impl(mt1, mt2, "not(a<b)", function(op) print(op, not(a<b)) end)
	run_test_impl(mt1, mt2, "not(a<=b)", function(op) print(op, not(a<=b)) end)
	run_test_impl(mt1, mt2, "not(a>b)", function(op) print(op, not(a>b)) end)
	run_test_impl(mt1, mt2, "not(a>=b)", function(op) print(op, not(a>=b)) end)
end

for i = 1,k do
	for j = 1,k do
		run_test(mtList[i], mtList[j])
	end
end

