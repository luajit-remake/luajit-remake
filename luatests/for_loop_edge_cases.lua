print('test1')
for i = 1, 5, 0 do
	print(i)
end
print('test2')
c = 0
for i = 5, 1, 0 do
	print(i)
	c = c + 1
	if (c == 3) then break end
end
print('test3')
for i = 1, 3, 0.0/0 do
	print(i)
end
print('test4')
for i = 3, 1, 0.0/0 do
	print(i)
end
print('test5')
for i = 0.0/0, 4, 1 do
	print(i)
end
print('test6')
for i = 4, 0.0/0, 1 do
	print(i)
end
print('test7')
for i = 0.0/0, 4, -1 do
	print(i)
end
print('test8')
for i = 4, 0.0/0, -1 do
	print(i)
end
print('test9')
for i = 0.0/0, 0.0/0, 1 do
	print(i)
end
print('test10')
for i = 0.0/0, 0.0/0, -1 do
	print(i)
end

