local cnt = 0
local function count(n)
	cnt = cnt + 1
	if n <= 0 then
		return 
	else
		return count(n-1)
	end
end
print(count(100000))
print(cnt)

cnt = 0
local function count2(n)
	cnt = cnt + 1
	if n <= 0 then
		return 123, 456, 789, 10
	else
		return count2(n-1)
	end
end
print(count2(100023))
print(cnt)


