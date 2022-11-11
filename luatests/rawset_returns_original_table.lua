t = {}
if (rawset(t, 'a', 1) ~= t) then
	print("bad")
end
print(t.a)

