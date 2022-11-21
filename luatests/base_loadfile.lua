print('executing loadfile')
local f = loadfile("luatests/base_loadfile_file.lua")
print(type(f))
print('executing func..')
print(f())
print('end of test')

