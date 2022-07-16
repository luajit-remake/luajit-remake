function f_bad(a, b)
	print('enter f_bad', a, b)
	error(true)
	print('should never reach here')
end

function f_good(a, b)
	print('enter f_good', a, b)
	return 233, 2333
end

print('test 1')
print(pcall(f_bad, 1, 2, 3, 4))
print('test 2')
print(pcall(f_good, 1, 2, 3, 4))

