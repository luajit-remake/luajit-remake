function g(dummy, n)
	local t = {}
	for i=1,n-1 do
		t[i] = i * 2
	end
	return t
end

function f(t)
	local sum = 0
	for k,v in pairs(t) do
		sum = sum + k + v
	end
	print(sum)
end

local tab = g(false, 30000)
f(tab)

