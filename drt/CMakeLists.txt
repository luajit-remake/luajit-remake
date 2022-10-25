add_library(deegen_rt 
	bytecode_builder.cpp
	deegen_internal_enter_exit_vm.s
)

add_dependencies(deegen_rt 
  deegen_fps_lib
)
set_target_properties(deegen_rt PROPERTIES COMPILE_FLAGS " -DDEEGEN_POST_FUTAMURA_PROJECTION ")

