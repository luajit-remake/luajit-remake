a={ name = "xxx" }
setmetatable(a, {
	__tostring = function(t)
		tostring = function()
			return "aaa"
		end
		return t.name
	end
})

print(1,2,3,a,1,2,3)
print(1,2,3,a,1,2,3)

