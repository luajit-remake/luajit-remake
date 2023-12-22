local a = 1
while a do
	local b = 2
	f = function()
		a = a + 1
		b = b + 1
	end
	if b then
		b = b + 1
	end
end
a = a + 1

