add_library(deegen_rt 
  type_speculation_printer.cpp
  llvm_ir_cleanup_pass.cpp
  llvm_extract_function.cpp
  deegen_desugaring_level.cpp
  tvalue_typecheck_optimization.cpp
  deegen_ast_make_call.cpp
  deegen_link_in_common_snippet.cpp
  deegen_bytecode_operand.cpp
  deegen_interpreter_interface.cpp
  deegen_ast_return.cpp
) 

