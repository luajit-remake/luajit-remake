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
  llvm_llc_helper.cpp
  deegen_process_lib_func.cpp
  deegen_ast_throw_error.cpp
  deegen_process_bytecode_definition_for_interpreter.cpp
  deegen_ast_simple_lowering_utils.cpp
  deegen_ast_get_global_object.cpp
  deegen_ast_guest_language_funtion_return.cpp
  deegen_ast_new_closure.cpp
  deegen_ast_upvalue_accessor.cpp
) 

# Build the main Futamura projection stage binary
#
add_executable(fps
  fps_main.cpp
  fps_utils.cpp
  fps_generate_interpreter_function_entry_logic.cpp
  fps_process_user_builtin_lib.cpp
  fps_process_bytecode_definition_for_interpreter.cpp
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

set(deegen_interpreter_function_entry_sources "")

# generate the interpreter function entry logic
#
set(hdr_name "${GENERATED_FILES_DIR}/get_guest_language_function_interpreter_entry_point.h")
set_source_files_properties(${hdr_name} PROPERTIES GENERATED true)

set(asm_name "${GENERATED_FILES_DIR}/guest_language_function_interpreter_entry_point_asm.s")
set_source_files_properties(${asm_name} PROPERTIES GENERATED true)
list(APPEND deegen_interpreter_function_entry_sources ${asm_name})

set(cpp_name "${GENERATED_FILES_DIR}/guest_language_function_interpreter_entry_point_cpp.cpp")
set_source_files_properties(${cpp_name} PROPERTIES GENERATED true)
list(APPEND deegen_interpreter_function_entry_sources ${cpp_name})

add_custom_command(
  OUTPUT ${hdr_name}
  OUTPUT ${asm_name}
  OUTPUT ${cpp_name}
  COMMAND ${fps_exec} --generate-interp-fn-entry-logic --hdr-output=${hdr_name} --asm-output=${asm_name} --cpp-output=${cpp_name}
  DEPENDS fps
)

add_library(deegen_interpreter_fn_entry_logic OBJECT
  ${deegen_interpreter_function_entry_sources}
)

