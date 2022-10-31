SET(DEEGEN_COMMON_SNIPPET_IR_SOURCES
  get_codeblock_from_stack_base.cpp
  get_bytecode_ptr_after_return_from_call.cpp
  copy_variadic_results_to_arguments_forward.cpp
  copy_variadic_results_to_arguments.cpp
  get_num_variadic_results.cpp
  get_end_of_call_frame.cpp
  populate_new_call_frame_header.cpp
  get_callee_entry_point.cpp
  move_call_frame_header_for_tail_call.cpp
  simple_left_to_right_copy_may_overcopy.cpp
  move_call_frame_for_tail_call.cpp
  move_variadic_results_for_variadic_notinplace_tailcall.cpp
  copy_variadic_results_to_arguments_for_variadic_notinplace_tailcall.cpp
  get_return_value_at_specified_ordinal.cpp
  store_first_k_return_values_padding_nil.cpp
  store_return_values_as_variadic_results.cpp
  simple_translate_to_raw_pointer.cpp
  populate_nil_to_unprovided_params.cpp
  get_num_fixed_params_from_code_block.cpp
  fixup_stack_frame_for_variadic_arg_function.cpp
  get_bytecode_ptr_from_codeblock.cpp
  populate_nil_for_return_values.cpp
  get_caller_stackbase_from_stack_base.cpp
  get_ret_addr_from_stack_base.cpp
  populate_new_call_frame_header_for_c_func.cpp
  get_global_object_from_code_block.cpp
  append_variadic_results_to_function_returns.cpp
  create_new_closure_from_code_block.cpp
  get_upvalue.cpp
  put_upvalue.cpp
  close_upvalues.cpp
  get_variadic_arg_start.cpp
  get_num_variadic_args.cpp
  store_variadic_args_as_variadic_results.cpp
  get_variadic_results_start.cpp
  box_function_object_to_tvalue.cpp
  get_cb_heap_ptr_from_tvalue.cpp
  get_func_obj_as_u64_from_tvalue.cpp
)

add_library(deegen_common_snippet_ir_sources OBJECT
  ${DEEGEN_COMMON_SNIPPET_IR_SOURCES}
)
set_target_properties(deegen_common_snippet_ir_sources PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} ")

add_executable(package_deegen_common_snippet_ir
  package_deegen_common_snippets_library.cpp
) 
target_link_libraries(package_deegen_common_snippet_ir PUBLIC
  ${LLVM_EXTRA_LINK_LIBRARIES} 
)
set_target_properties(package_deegen_common_snippet_ir PROPERTIES COMPILE_FLAGS " -O3 ")

set(generated_src_list "")
foreach(cur_src ${DEEGEN_COMMON_SNIPPET_IR_SOURCES})
  set(cur_generated_src "${GENERATED_FILES_DIR}/deegen_common_snippet.${cur_src}.ir.cpp")
  list(APPEND generated_src_list "${cur_generated_src}")
  set_source_files_properties(${cur_generated_src} PROPERTIES GENERATED true)
endforeach()

set_source_files_properties(${GENERATED_FILES_DIR}/deegen_common_snippet_ir_accessor.cpp PROPERTIES GENERATED true)

add_custom_command(
  OUTPUT ${GENERATED_FILES_DIR}/deegen_common_snippet_ir_accessor.cpp
  OUTPUT ${generated_src_list}
  COMMAND ${PROJECT_BINARY_DIR}/annotated/deegen_common_snippets/package_deegen_common_snippet_ir '$<TARGET_OBJECTS:deegen_common_snippet_ir_sources>' "${GENERATED_FILES_DIR}"
  DEPENDS $<TARGET_OBJECTS:deegen_common_snippet_ir_sources> package_deegen_common_snippet_ir 
)

add_library(deegen_common_snippet_ir 
  ${GENERATED_FILES_DIR}/deegen_common_snippet_ir_accessor.cpp
  ${generated_src_list}
)
set_target_properties(deegen_common_snippet_ir PROPERTIES COMPILE_FLAGS " -O3 ")

