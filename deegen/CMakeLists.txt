add_library(deegen_rt 
  type_speculation_printer.cpp
  llvm_ir_cleanup_pass.cpp
  llvm_extract_function.cpp
  deegen_desugaring_level.cpp
  tvalue_typecheck_optimization.cpp
  deegen_ast_make_call.cpp
  deegen_link_in_common_snippet.cpp
  deegen_bytecode_operand.cpp
  deegen_interpreter_function_interface.cpp
  deegen_ast_return.cpp
  deegen_ast_return_value_accessor.cpp
  deegen_interpreter_bytecode_impl_creator.cpp
  deegen_interpreter_function_entry.cpp
) 

# Build the main Futamura projection stage binary
#
add_executable(fps
  fps_main.cpp        
)

target_link_libraries(fps PUBLIC
  -Wl,--start-group
  common_utils
  deegen_rt
  deegen_common_snippet_ir
  -Wl,--end-group
  ${LLVM_EXTRA_LINK_LIBRARIES} 
)

set(fps_exec "${PROJECT_BINARY_DIR}/deegen/fps")

