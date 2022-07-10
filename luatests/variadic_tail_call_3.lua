local cnt = 0
local cnt2 = 0
local cnt3 = 0
local f, g, h, i

f = function(n, ...)
	if n == 0 then return end 
	cnt = cnt + 1
	return g(n - 1, 1, ...)
end

g = function(n, ...)
	if n == 0 then return end 
	cnt = cnt + 1
	return h(n - 1, 2, ...)
end

h = function(n, x, ...)
	if n == 0 then return end 
	cnt = cnt + 1
	cnt2 = cnt2 + x
	return i(n - 1, ...)
end

i = function(n, x, ...)
	if n == 0 then return end 
	cnt = cnt + 1
	cnt3 = cnt3 + x
	return f(n-1, ...)
end

print(f(100000))
print(cnt, cnt2, cnt3)

