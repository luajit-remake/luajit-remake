function run_func(input, base)
	if (base == nil) then
		return tonumber(input)
	else
		return tonumber(input, base)
	end
end

function check_expect_succeed(input, base, expected)
	local res = run_func(input, base)
	assert(type(res) == "number")
	assert(res == expected)
	assert(rawequal(res, expected))
end

function check_expect_fail(input, base)
	local res = run_func(input, base)
	assert(type(res) == "nil")
	assert(res == nil)
	assert(rawequal(res, nil))
end

function check_expect_throw(input, base)
	local res = pcall((function()
		run_func(input, base)
		print("should never reach here")
	end))
	assert(res == false)
end

assert(tonumber(12.3, nil) == 12.3)
assert(tonumber("12.34", nil) == 12.34)
assert(tonumber("AA", nil) == nil)
assert(tonumber("0xFF", nil) == 255)

check_expect_succeed(100, nil, 100)
check_expect_succeed(123.4, nil, 123.4)
check_expect_succeed("100", nil, 100)
check_expect_succeed("123.4", nil, 123.4)
check_expect_succeed("  0xFF  ", 16, 255)
check_expect_succeed("  FF  ", 16, 255)
check_expect_succeed("  FF  ", " 16 ", 255)
check_expect_succeed("  0xFF  ", 10, 255)
check_expect_succeed("  0xFF  ", nil, 255)
check_expect_succeed("  ZZ  ", 36, 36 * 36 - 1)
check_expect_succeed("  10101 ", "   2   ", 21)

check_expect_fail("2", 2)
check_expect_fail("A", 10)
check_expect_fail("Z", 35)

-- yes this is Lua behavor: if input is not string, if base is not provided, 
-- is nil or is 10, no error is thrown and the function returns nil. 
-- But otherwise an error is thrown.
assert(tonumber({}, nil) == nil)
check_expect_fail({}, nil)
check_expect_fail({}, 10)
check_expect_throw({}, 2)

assert(tonumber(function()end, nil) == nil)
check_expect_fail(function()end, nil)
check_expect_fail(function()end, 10)
check_expect_throw(function()end, 2)

check_expect_throw(1, 37)
check_expect_throw(0, 1)
check_expect_throw(1, "a")
check_expect_throw(1, {})

print('test end')

