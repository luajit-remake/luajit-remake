sieve = function(n)
	local cnt = 0
	local lp = {}
	local pr = {}
	for i = 1, n, 1 do
		lp[i] = 0
	end
	for i = 2, n, 1 do
		if lp[i] == 0 then
			lp[i] = i
			cnt = cnt + 1
			pr[cnt] = i
		end
		local j = 1
		while j <= cnt and pr[j] <= lp[i] and i * pr[j] <= n do
			lp[i * pr[j]] = pr[j]
			j = j + 1
		end
	end
	return cnt
end

print(sieve(100000))

