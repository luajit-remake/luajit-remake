function f_bad(a, b)
	print('enter f_bad', a, b)
	error(true)
	print('should never reach here')
end

function f_good(a, b)
	print('enter f_good', a, b)
	return 233, 2333
end

function err_handler(a, b)
	print('enter err handler', a, b)
	return 123, 456
end

print('test 1')
print(xpcall(f_bad, err_handler))
print('test 2')
print(xpcall(f_good, err_handler))

