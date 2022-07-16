cnt = 0

g = function(err, x)
	print('enter g', err, x)
	cnt = cnt + 1
	if cnt < 10 then
		print('throwing error')
		if cnt % 2 == 0 then
			error(false)
		else
			error(true)
		end
		print('should never reach here')
	else
		return 1,2,3,4
	end
end

print(xpcall(nil, g))

