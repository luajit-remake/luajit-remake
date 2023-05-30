-- reported by XmiliaH  
local tab = {a = 1, b = 2, c = 3}
local seen = {}
seen[next(tab)] = true 
for k, v in next, tab, next(tab) do
	assert((k == 'a' and v == 1) or (k == 'b' and v == 2) or (k == 'c' and v == 3))
	assert(seen[k] == nil)
	seen[k] = true
	print('Loop body executed once')
end
assert(seen.a == true and seen.b == true and seen.c == true)
print('OK')

 
