function g(dummy, n)
	local t = {}
	for i=1,n-1 do
		t[i] = i * 2
	end
	return t
end

function f(t)
	local sum = 0
	for k in pairs(t) do
		sum = sum + k
	end
	print(sum)
end

local tab = g(false, 50000)
f(tab)

