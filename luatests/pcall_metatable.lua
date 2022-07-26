function g(a, b, c, d, e)
	print('enter g', a.name, b, c, d, e)
	error(true)
	print('should never reach here')
end

gg = { name = 'gg' }
setmetatable(gg, {
	__call = g
})

print(pcall(gg, 1, 2))
 
function g2(a, b, c, d, e)
	print('enter g2', a.name, b, c, d, e)
	return 1,2,3,4
end

getmetatable(gg).__call = g2

print(pcall(gg, 1, 2))


