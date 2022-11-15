old = tostring
tostring = function(t)
	if (type(t) == "table") then
		return old(t)
	else
		return "aaa"
	end
end

a={ name = "xxx" }
setmetatable(a, {
	__tostring = function(t)
		tostring = old
		return t.name
	end
})

print(1,2,3,a,1,2,3)
print(1,2,3,a,1,2,3)
print(1,2,3,a,1,2,3)

