coro = coroutine.wrap(function()
	local a,b,c
	a,b,c = coroutine.yield()
	print(a,b,c)
end)

print(coro())
print(coro())

