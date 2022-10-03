add_library(toylang 
  bytecode.cpp  
)

add_dependencies(toylang 
  deegen_fps_lib
)
set_target_properties(toylang PROPERTIES COMPILE_FLAGS " -DDEEGEN_POST_FUTAMURA_PROJECTION ")

