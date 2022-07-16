-- test various edge cases of xpcall

function h()
	print('enter h')
	return 123
end

function test()
	xpcall()
	print('should never reach here')
end

-- if not enough params are passed, raise an error that is not protected by this xpcall 
print('test1')
print(xpcall(test, h))

function test2()
	xpcall(h)
	print('should never reach here')
end

-- if not enough params are passed, raise an error that is not protected by this xpcall 
print('test2')
print(xpcall(test2, h))

function f(a)
	print('enter f', a)
	return 123, 456, 789
end

-- if error handler is not a function, should still work if no error is raised (Lua 5.1 behavior)
print('test3')
print(xpcall(f, nil))

function f2(a)
	print('enter f2', a)
	error(true)
end

-- if error handler is not a function, should generate "error in error handler" if the function errored out (Lua 5.1 behavior)
print('test4')
print(xpcall(f2, nil))

print('test5')
print(xpcall(nil, nil))

print('test6')
print(xpcall({}, nil))

function hh(err)
	print('enter h', err)
	return 124, 421
end

-- if the first function is not callable, the error is protected by xpcall
print('test7')
print(xpcall(nil, hh))

print('test8')
print(xpcall({}, hh))

-- test infinite error recursion
function g()
	error(true)
end

print('test10')
print(xpcall(g, g))

-- test too many parameters passed to xpcall (just for sanity)
function f3(a, b)
	print('enter f3', a, b)
	return 21, 43
end

print('test11')
print(xpcall(f3, f3, g, g, h, h))

function f4(a, b)
	print('enter f4', a, b)
	error(false)
end

print('test12')
print(xpcall(f4, h, f4, f4, f4, f4))



