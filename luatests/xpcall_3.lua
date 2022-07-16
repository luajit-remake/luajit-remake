-- test xpcall in error handler
 
cnt = 0

f = function(a, b)
	print('enter f', a, b)
	error(true)
end

g = function(err, x)
	print('enter g', err, x)
	local val = cnt
	cnt = cnt + 1
	if cnt < 5 then
		print('doing xpcall')
		print('returned from xpcall', xpcall(f, g))
		return val, val+1
	else
		return val + 10, val + 20
	end
end

print(xpcall(f, g))
 
