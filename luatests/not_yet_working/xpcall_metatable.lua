function g(a, b)
	print('enter g', a, b)
	error()
end

gg = {}
setmetatable(gg, {
	__call = g
})

function h(err)
	print('enter h')
	print(err)
	return 123
end

print(xpcall(gg, h))
 
