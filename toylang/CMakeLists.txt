add_library(toylang 
  runtime_utils.cpp
  bytecode_new.cpp
  init_global_object.cpp
)

add_dependencies(toylang 
  deegen_fps_lib
)
set_target_properties(toylang PROPERTIES COMPILE_FLAGS " -DDEEGEN_POST_FUTAMURA_PROJECTION ")

