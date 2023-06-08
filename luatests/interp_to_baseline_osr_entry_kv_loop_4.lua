function g(dummy, n)
	local t = {}
	for i=1,n-1 do
		t[i] = i * 2
	end
	return t
end

function f(t)
	local sum = 0
	local tmp = next(t)
	sum = sum + tmp + t[tmp]
	for k,v in next, t, tmp do
		sum = sum + k + v
	end
	print(sum)
end

local tab = g(false, 30000)
f(tab)

