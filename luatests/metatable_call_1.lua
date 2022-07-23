

function f(a, b, c, d)
	print(a,b,c,d)
	return 1,2,3
end

debug.setmetatable(nil, {
	__call = f
})

g = nil
g(1)

debug.setmetatable(false, {
	__call = f
})

g = false
g(1)

g = true
g(1)


h = {}
debug.setmetatable("2", {
	__call = f
})

g = {}
setmetatable(g, {
	__call = h
})

g();
 
