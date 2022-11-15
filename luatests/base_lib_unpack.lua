t1 = { [1] = 'a', [2] = 'b', [3] = 'c', [4] = 'd', [5] = 'e', [6] = 'f', [7] = 'g' }

print(unpack(t1))
print(unpack(t1, 10))
print(unpack(t1, 2))
print(unpack(t1, -1))
print(unpack(t1, -10))
print(unpack(t1, 4, 3))
print(unpack(t1, 1, 2))
print(unpack(t1, 3, 4))
print(unpack(t1, 3, 8))
print(unpack(t1, 3, 20))
print(unpack(t1, -10, 10))
print(unpack(t1, -10, -5))
print(unpack(t1, 10, 20))
print(unpack(t1, -3, 1))
print(unpack(t1, -3, 2))
print(unpack(t1, 7, 8))
print(unpack(t1, 6, 9))
print(unpack(t1, 4, 7))
print(unpack(t1, 1, 3))

t2 = { [-1] = 'a', [0] = 'b', [1] = 'c', [2] = 'd', [10] = 'e', [100000] = 'f', [100001] = 'g' }

print(unpack(t2, 1, 2))
print(unpack(t2, -2, 2))
print(unpack(t2, -2, 4))
print(unpack(t2, 99999, 100002))

