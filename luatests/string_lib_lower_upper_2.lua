print(string.upper('aAbBcCdDzZ12345,./'))
print(string.lower('aAbBcCdDzZ12345,./'))

print(type(string.upper(123)))
print(string.upper(123))
print(type(string.upper(1.0001)))
print(string.upper(1.0001))
print(type(string.upper(123456781234567812345678)))
print(string.upper(123456781234567812345678))

print(type(string.lower(123)))
print(string.lower(123))
print(type(string.lower(1.0001)))
print(string.lower(1.0001))
print(type(string.lower(123456781234567812345678)))
print(string.lower(123456781234567812345678))

s = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
for i = 1,9 do
	s = s .. s
end
print(string.upper(s))
print(string.lower(s))
