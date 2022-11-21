print('executing dofile')
local a = 1001
b = 1002
print(a,b)
print(dofile('luatests/base_lib_dofile_file.lua'))
print(a,b)
print('finished')

