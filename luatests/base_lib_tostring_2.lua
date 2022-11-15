a={ name = "xxx" }
setmetatable(a, {
	__tostring = function(t)
		debug.setmetatable(2, {
			__tostring = function(y)
				return y + 2000
			end
		})
		return t.name
	end
})

print(1,2,3,a,1,2,3)


