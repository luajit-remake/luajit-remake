f = function(a, b, c)
	print('enter f', a, b, c)
	return
end

debug.setmetatable("1", { __add = f })

print("1 " + "  0x2")
print("a" + "1")
print("1" + "a")
print(1.2 + "0xF")
print(1.2 + "0xG")
print("0xF" + 1.2)
print("0xG" + 1.2)
print("    0xF        " + 3)
print("    0xG        " + 3)
print("0xF" + " 0xF ")
print("0xG" + " 0xG ")

