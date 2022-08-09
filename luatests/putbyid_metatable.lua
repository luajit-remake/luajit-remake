print('-- test 1 --')

cnt = 0
t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print("should never reach here")
	end
})

rawset(t, "a", 123)
print(i, t.a)
t.a = 234
print(i, t.a)

print('-- test 2 --')

cnt = 0
t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print(a.name, b, c, d)
		cnt = cnt + 1
		if cnt == 3 then
			rawset(a, b, c * 2)
		end
		return
	end
})

for i = 1,6 do
	print("before", i, t.a)
	t.a = 123
	print("after", i, t.a)
end

print('-- test 2 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = function(a, b, c, d)
		print("f1", a.name, b, c, d)
		setmetatable(a, {
			__newindex = function(a, b, c, d) 
				print("f2", a.name, b, c, d)
				setmetatable(a, {})
				a[b] = c * 3
			end
		})
		a.xx = c * 2
	end
})
t.xx = 20
print(t.xx)
t.xx = 30
print(t.xx)

print('-- test 3 --')

t = { name = "t" }
setmetatable(t, {
	__newindex = 233
})

print((pcall(function() t.yy = 1 end)))

debug.setmetatable(233, {
	__newindex = function(a,b,c,d,e)
		print("f3", a,b,c,d,e)
	end
})
t.zz = 12
print(t.zz)
t.zz = 23
print(t.zz)



