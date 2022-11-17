total_tests = 0
total_checks = 0

function run_test(array_getter, arr, cmp)
	local n = #arr
	local t = array_getter(arr)
	local cnt = {}
	for i = 1,n do
		local v = t[i]
		if cnt[v] == nil then
			cnt[v] = 0
		end
		cnt[v] = cnt[v] + 1
	end
	table.sort(t, cmp)
	if cmp == nil then
		for i = 1, n-1 do
			assert(not(t[i+1] < t[i]))
			assert(t[i].idx <= t[i+1].idx)
			total_checks = total_checks + 1
		end
	else
		for i = 1, n-1 do
			assert(not cmp(t[i + 1], t[i]))
			assert(t[i].idx >= t[i+1].idx)
			total_checks = total_checks + 1
		end
	end
	for i = 1,n do
		local v = t[i]
		assert(cnt[v] ~= nil)
		cnt[v] = cnt[v] - 1
		total_checks = total_checks + 1
	end
	for k,v in pairs(cnt) do
		assert(v == 0)
		total_checks = total_checks + 1
	end
	total_tests = total_tests + 1
end

function cont_array(arr)
	local n = #arr
	local t = {}
	for i = 1,n do
		t[i] = arr[i]
	end
	return t
end

function not_cont_array(arr)
	local n = #arr
	local t = {}
	for i = n,1,-1 do
		t[i] = arr[i]
	end
	return t
end

meta = {
	__lt = function(x,y)
		return x.idx < y.idx
	end
}

function cmpFn(x, y)
	return x.idx > y.idx
end

function mt(k)
	return setmetatable({idx = k}, meta)
end

function nm(k)
	return {idx = k}
end

function get_arr_mt(arr)
	local n = #arr
	local arr1 = {}
	for i = 1,n do
		arr1[i] = mt(arr[i])
	end
	return arr1
end

function get_arr_nm(arr)
	local n = #arr
	local arr1 = {}
	for i = 1,n do
		arr1[i] = nm(arr[i])
	end
	return arr1
end

function run_helper(arr)
	run_test(cont_array, get_arr_mt(arr), nil)
	run_test(not_cont_array, get_arr_mt(arr), nil)
	run_test(cont_array, get_arr_nm(arr), cmpFn)
	run_test(not_cont_array, get_arr_nm(arr), cmpFn)
	run_test(cont_array, get_arr_mt(arr), cmpFn)
	run_test(not_cont_array, get_arr_mt(arr), cmpFn)
end

for i1 = 0,4 do
	run_helper({[1] = i1})
end
for i1 = 0,4 do
	for i2 = 0,4 do
		run_helper({[1] = i1, [2] = i2})
	end
end
for i1 = 0,4 do
	for i2 = 0,4 do
		for i3 = 0,4 do
			run_helper({[1] = i1, [2] = i2, [3] = i3})
		end
	end
end
for i1 = 0,4 do
	for i2 = 0,4 do
		for i3 = 0,4 do
			for i4 = 0,4 do
				run_helper({[1] = i1, [2] = i2, [3] = i3, [4] = i4})
			end
		end
	end
end
for i1 = 0,4 do
	for i2 = 0,4 do
		for i3 = 0,4 do
			for i4 = 0,4 do
				for i5 = 0,4 do
					run_helper({[1] = i1, [2] = i2, [3] = i3, [4] = i4, [5] = i5})
				end
			end
		end
	end
end

print(string.format('test ok, total tests = %d, total checks = %d', total_tests, total_checks))


