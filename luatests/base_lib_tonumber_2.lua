print(tonumber(111, 2))
print(tonumber(111.1, 2))
-- lua stringifies the number to string *using lua default options* and then try to convert the string to number again
-- this means that despite 1.00000000000001 != 1 (double is accurate enough to hold it), it will become 1 after stringify, and tonumber will succeed
-- but 1.0000000000001, having one less 0, will still be "1.0000000000001" after stringify, and tonumber will fail
print(tonumber(1.0000000000001, 2))
print(tonumber(1.00000000000001, 2))
-- but if the input is passed in as string, tonumber will always fail
print(tonumber("1.000000000001", 2))
print(tonumber("1.0000000000001", 2))

