local cnt = 0
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

function g(n)
	if n == 0 then
		return
	end
	return g(n - 1, f())
end

print(g(100000))
print(cnt)

