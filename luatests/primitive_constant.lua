local eq_true = function(a)
	if a == true then
		print('1')
	else
		print('0')
	end
end

local eq_false = function(a)
	if a == false then
		print('1')
	else
		print('0')
	end
end

local eq_nil = function(a)
	if a == nil then
		print('1')
	else
		print('0')
	end
end

local ne_true = function(a)
	if a ~= true then
		print('1')
	else
		print('0')
	end
end

local ne_false = function(a)
	if a ~= false then
		print('1')
	else
		print('0')
	end
end

local ne_nil = function(a)
	if a ~= nil then
		print('1')
	else
		print('0')
	end
end

eq_true(1)
eq_true(true)
eq_true(false)
eq_true(nil)
eq_true(0.0/0)
eq_true({ x = 1 })

eq_false(1)
eq_false(true)
eq_false(false)
eq_false(nil)
eq_false(0.0/0)
eq_false({ x = 1 })

eq_nil(1)
eq_nil(true)
eq_nil(false)
eq_nil(nil)
eq_nil(0.0/0)
eq_nil({ x = 1 })

ne_true(1)
ne_true(true)
ne_true(false)
ne_true(nil)
ne_true(0.0/0)
ne_true({ x = 1 })

ne_false(1)
ne_false(true)
ne_false(false)
ne_false(nil)
ne_false(0.0/0)
ne_false({ x = 1 })

ne_nil(1)
ne_nil(true)
ne_nil(false)
ne_nil(nil)
ne_nil(0.0/0)
ne_nil({ x = 1 })

-- test USETP

local function f()
	local val = 0
	return {
		set_false = function()
			val = false
		end,
		set_true = function()
			val = true
		end,
		set_nil = function()
			val = nil
		end,
		set_zero = function()
			val = 0
		end,
		print_val = function()
			print(val)
		end
	}
end

local o = f()
o.print_val()
o.set_false()
o.print_val()
o.set_true()
o.print_val()
o.set_nil()
o.print_val()
o.set_zero()
o.print_val()
o.set_true()
o.print_val()
local o2 = f()
o2.print_val()
o.print_val()

		
		
