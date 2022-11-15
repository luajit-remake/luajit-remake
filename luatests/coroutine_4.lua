coro = coroutine.create(function()
	local a,b,c
	a,b,c = coroutine.yield()
	print(a,b,c)
end)

print(coroutine.resume(coro))
print(coroutine.resume(coro))


