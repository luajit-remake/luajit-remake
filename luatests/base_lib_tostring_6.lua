mt = {
	__tostring = function(t)
		return t.name
	end
}

a={ name = "xxx" }
setmetatable(a, mt)

b={ name = "yyy" }
setmetatable(b, mt)

c={ name = "zzz" }
setmetatable(c, mt)

print(a,123,b,124,c,125,126)
print(123,a,124,b,125,c,126)
print(a,b,c,123,124,125,126)
print(123,124,125,126,a,b,c)
print(a,b,c,123,124,125,126,a,b,c)
print(123,124,125,126,a,b,c,123,124,125,126)

