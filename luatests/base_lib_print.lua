-- test yield during metamethod invoked by print

mt = {
	__tostring = function(t)
		local x = coroutine.yield(t.name)
		return x
	end
}

a={ name = "xxx" }
setmetatable(a, mt)

b={ name = "yyy" }
setmetatable(b, mt)

c={ name = "zzz" }
setmetatable(c, mt)

coro = coroutine.create(function()
	print(a,123,b,124,c,125,126)
	return "finish"
end)

ok1, v1 = coroutine.resume(coro)
ok2, v2 = coroutine.resume(coro, "aaa")
ok3, v3 = coroutine.resume(coro, "bbb")
ok4, v4 = coroutine.resume(coro, "ccc")

print(coroutine.status(coro))
print(ok1, v1)
print(ok2, v2)
print(ok3, v3)
print(ok4, v4)


