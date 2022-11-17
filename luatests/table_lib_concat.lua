t = { [-5] = 'a', [-4] = 'b', [-3] = 'c', [-2] = 'd', [-1] = 'e', [0] = 'f', [1] = 'g', [2] = 'h', [3] = 'i', [4] = 'j', [5] = 'k' }

print(table.concat(t))
print(table.concat(t,nil))
print(table.concat(t,nil,-5))
print(table.concat(t,nil,2))
print(table.concat(t,nil,10))
print(table.concat(t,nil,-5,-4))
print(table.concat(t,nil,-5,-6))
print(table.concat(t,nil,-4,-2))
print(table.concat(t,nil,-4,4))
print(table.concat(t,nil,1,5))
print(table.concat(t,nil,2,4))

print(table.concat(t,"AA"))
print(table.concat(t,"AA",-5))
print(table.concat(t,"AA",2))
print(table.concat(t,"AA",10))
print(table.concat(t,"AA",-5,-4))
print(table.concat(t,"AA",-5,-6))
print(table.concat(t,"AA",-4,-2))
print(table.concat(t,"AA",-4,4))
print(table.concat(t,"AA",1,5))
print(table.concat(t,"AA",2,4))

print(table.concat(t,1.23))
print(table.concat(t,1.23,-5))
print(table.concat(t,1.23,2))
print(table.concat(t,1.23,10))
print(table.concat(t,1.23,-5,-4))
print(table.concat(t,1.23,-5,-6))
print(table.concat(t,1.23,-4,-2))
print(table.concat(t,1.23,-4,4))
print(table.concat(t,1.23,1,5))
print(table.concat(t,1.23,2,4))

t = { [-5] = -105, [-4] = -104, [-3] = -103, [-2] = -102, [-1] = -101, [0] = -100, [1] = -99, [2] = -98, [3] = -97, [4] = -96, [5] = -95 }

print(table.concat(t))
print(table.concat(t,nil))
print(table.concat(t,nil,-5))
print(table.concat(t,nil,2))
print(table.concat(t,nil,10))
print(table.concat(t,nil,-5,-4))
print(table.concat(t,nil,-5,-6))
print(table.concat(t,nil,-4,-2))
print(table.concat(t,nil,-4,4))
print(table.concat(t,nil,1,5))
print(table.concat(t,nil,2,4))

print(table.concat(t,"BB"))
print(table.concat(t,"BB",-5))
print(table.concat(t,"BB",2))
print(table.concat(t,"BB",10))
print(table.concat(t,"BB",-5,-4))
print(table.concat(t,"BB",-5,-6))
print(table.concat(t,"BB",-4,-2))
print(table.concat(t,"BB",-4,4))
print(table.concat(t,"BB",1,5))
print(table.concat(t,"BB",2,4))

print(table.concat(t,1.23))
print(table.concat(t,1.23,-5))
print(table.concat(t,1.23,2))
print(table.concat(t,1.23,10))
print(table.concat(t,1.23,-5,-4))
print(table.concat(t,1.23,-5,-6))
print(table.concat(t,1.23,-4,-2))
print(table.concat(t,1.23,-4,4))
print(table.concat(t,1.23,1,5))
print(table.concat(t,1.23,2,4))

t = { [-5] = -105, [-4] = 'qq', [-3] = -103, [-2] = 'ww', [-1] = -101, [0] = 'ee', [1] = -99, [2] = 'rr', [3] = -97, [4] = 'tt', [5] = -95 }

print(table.concat(t))
print(table.concat(t,nil))
print(table.concat(t,nil,-5))
print(table.concat(t,nil,2))
print(table.concat(t,nil,10))
print(table.concat(t,nil,-5,-4))
print(table.concat(t,nil,-5,-6))
print(table.concat(t,nil,-4,-2))
print(table.concat(t,nil,-4,4))
print(table.concat(t,nil,1,5))
print(table.concat(t,nil,2,4))

print(table.concat(t,"CC"))
print(table.concat(t,"CC",-5))
print(table.concat(t,"CC",2))
print(table.concat(t,"CC",10))
print(table.concat(t,"CC",-5,-4))
print(table.concat(t,"CC",-5,-6))
print(table.concat(t,"CC",-4,-2))
print(table.concat(t,"CC",-4,4))
print(table.concat(t,"CC",1,5))
print(table.concat(t,"CC",2,4))

print(table.concat(t,""))
print(table.concat(t,"",-5))
print(table.concat(t,"",2))
print(table.concat(t,"",10))
print(table.concat(t,"",-5,-4))
print(table.concat(t,"",-5,-6))
print(table.concat(t,"",-4,-2))
print(table.concat(t,"",-4,4))
print(table.concat(t,"",1,5))
print(table.concat(t,"",2,4))

print(table.concat(t,1.23))
print(table.concat(t,1.23,-5))
print(table.concat(t,1.23,2))
print(table.concat(t,1.23,10))
print(table.concat(t,1.23,-5,-4))
print(table.concat(t,1.23,-5,-6))
print(table.concat(t,1.23,-4,-2))
print(table.concat(t,1.23,-4,4))
print(table.concat(t,1.23,1,5))
print(table.concat(t,1.23,2,4))

for i=1,3000 do
	t[i] = i
end
print(table.concat(t))
print(table.concat(t, "XX"))
print(table.concat(t, 9.87))

for i=1,3000 do
	t[i] = "a"..i
end
print(table.concat(t))
print(table.concat(t, "YY"))
print(table.concat(t, 9.87))

for i=1,3000 do
	if i % 2 == 0 then
		t[i] = "a"..i
	else
		t[i] = i
	end
end
print(table.concat(t))
print(table.concat(t, "ZZ"))
print(table.concat(t, 9.87))

