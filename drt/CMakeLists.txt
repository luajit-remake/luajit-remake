add_library(deegen_rt 
	bytecode_builder.cpp
)

add_dependencies(deegen_rt 
  deegen_fps_lib
)
set_target_properties(deegen_rt PROPERTIES COMPILE_FLAGS " -DDEEGEN_POST_FUTAMURA_PROJECTION ")

