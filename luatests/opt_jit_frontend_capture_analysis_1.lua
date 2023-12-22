local a = 1
local b = 2
local c = 3
if a + b == c then
	f = function()
		a = a + 1
	end
else
	g = function()
		b = b + 1
	end
end
a = a + 1
b = b + 1
c = c + 1
h = function()
	c = c + 1
end
if a + b == 4 then
	i = function()
		a = a + 2
	end
else
	j = function()
		b = b + 2
	end
end
a = a + 1
b = b + 1
c = c + 1

