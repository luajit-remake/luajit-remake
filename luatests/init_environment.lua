local function count_field(o)
	local cnt = 0
	local k, v
	for k, v in pairs(o) do
		cnt = cnt + 1
	end
	return cnt
end

-- create the hidden global variable 'arg' if it has not been created yet
arg = {}

print('_G:', count_field(_G))
print('lib coroutine:', count_field(coroutine))
print('lib debug:', count_field(debug))
-- print('lib io:', count_field(io))
print('lib math:', count_field(math))
print('lib os:', count_field(os))
-- print('lib package:', count_field(package))
print('lib string:', count_field(string))

