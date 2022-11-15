-- This test should only work for LuaJIT Remake, not Lua or LuaJIT, since we removed yield limitations.
print('-- yield in error handler (test 1) --')

coro = coroutine.create(function()
	print(xpcall(function()
		error(false)
		print("should never reach here")
	end, 
	function(err)
		print('in error handler, err=', err)
		a,b = coroutine.yield(1,2)
		print('in error handler, err=', err,'a=',a,'b=',b)
		a,b = coroutine.yield(a+1,b+1)
		print('in error handler, err=', err,'a=',a,'b=',b)
		return "return from error handler"
	end))
end)
print(coroutine.resume(coro))
print(coroutine.status(coro))
print(coroutine.resume(coro,10,11))
print(coroutine.status(coro))
print(coroutine.resume(coro,12,13))
print(coroutine.status(coro))

print('-- yield in error handler (test 2) --')

coro = coroutine.wrap(function()
	print(xpcall(function()
		error(false)
		print("should never reach here")
	end, 
	function(err)
		print('in error handler, err=', err)
		a,b = coroutine.yield(1,2)
		print('in error handler, err=', err,'a=',a,'b=',b)
		a,b = coroutine.yield(a+1,b+1)
		print('in error handler, err=', err,'a=',a,'b=',b)
		return "return from error handler"
	end))
end)
print(coro())
print(coro(10,11))
print(coro(12,13))

print('-- coroutine in error handler (test 3) --')

coro = coroutine.wrap(function()
	print(xpcall(function()
		error(false)
		print("should never reach here")
	end, 
	function(err)
		print('in error handler, err=', err)
		coro2 = coroutine.create(function()
			print('in coro2')
			a,b = coroutine.yield(1,2)
			print('in coro2, a=',a, 'b=',b)
			return "return from coro2"
		end)
		a,b = coroutine.resume(coro2)
		print('in error handler, err=', err,'a=',a,'b=',b)
		a,b = coroutine.yield(a,b)
		print('in error handler, err=', err,'a=',a,'b=',b)
		a,b = coroutine.resume(coro2)
		print('in error handler, err=', err,'a=',a,'b=',b)
		return "return from error handler"
	end))
end)
print(coro())
print(coro(10,11))


