print('in main')
print(pcall(function() dofile('luatests/base_lib_dofile_throw_file.lua') print('should never reach here') end))

