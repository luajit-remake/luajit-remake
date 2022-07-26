function g(a, b)
	print('enter g', a.name, b)
	error(true)
	print('should never reach here')
end

gg = { name = 'gg' }
setmetatable(gg, {
	__call = g
})

function h(err)
	print('enter h')
	print(err)
	return 123, 456
end

print(xpcall(gg, h)) 

function g2(a, b)
	print('enter g2', a.name, b)
	return 789, 10
end

getmetatable(gg).__call = g2

print(xpcall(gg, h))
