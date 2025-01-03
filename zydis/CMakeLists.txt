add_library(zydis 
  zydis.c
)

SET(CMAKE_C_FLAGS  " ")

set_source_files_properties(zydis.c PROPERTIES COMPILE_FLAGS " -O3 ")
