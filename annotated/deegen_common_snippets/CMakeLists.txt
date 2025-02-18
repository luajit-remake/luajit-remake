SET(DEEGEN_COMMON_SNIPPET_IR_SOURCES
  get_codeblock_from_stack_base.cpp
  get_bytecode_ptr_after_return_from_call.cpp
  copy_variadic_results_to_arguments_forward.cpp
  copy_variadic_results_to_arguments.cpp
  get_num_variadic_results.cpp
  get_end_of_call_frame_from_interpreter_codeblock.cpp
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
  box_function_object_heap_ptr_to_tvalue.cpp
  get_cb_heap_ptr_from_tvalue.cpp
  get_func_obj_as_u64_from_tvalue.cpp
  get_immutable_upvalue.cpp
  get_mutable_upvalue.cpp
  get_bytecode_ptr_from_bytecode_offset.cpp
  translate_raw_pointer_to_sysheap_pointer.cpp
  get_jit_slowpath_data_after_slow_call.cpp
  round_up_to_multiple_of.cpp
  get_baseline_jit_codeblock_from_codeblock.cpp
  get_callee_cb_u32.cpp
  check_is_tvalue_heap_entity.cpp
  create_new_jit_call_ic_for_direct_call_mode_site.cpp
  create_new_jit_call_ic_for_closure_call_mode_site.cpp
  initialize_jit_call_ic_site.cpp
  initialize_jit_generic_ic_site.cpp
  create_new_jit_generic_ic_for_baseline_jit.cpp
  update_interpreter_tier_up_counter_for_return_or_throw.cpp
  update_interpreter_tier_up_counter_for_branch.cpp
  get_interpreter_tier_up_counter.cpp
  get_interpreter_tier_up_counter_from_cb_heap_ptr.cpp
  tier_up_into_baseline_jit.cpp
  update_interpreter_call_ic_doubly_link.cpp
  osr_entry_into_baseline_jit.cpp
  get_baseline_codeblock_from_stack_base.cpp
  get_end_of_call_frame_from_baseline_codeblock.cpp
  get_baseline_jit_codeblock_from_codeblock_heap_ptr.cpp
  get_global_object_from_baseline_code_block.cpp
  get_dfg_codeblock_from_stack_base.cpp
  get_global_object_from_dfg_code_block.cpp
  get_reg_spill_region_offset_from_dfg_code_block.cpp
  apply_dfg_runtime_reg_patch_data.cpp
  apply_dfg_runtime_reg_patch_using_slow_path_data_reg_config.cpp
  get_dfg_physical_slot_for_ssa_input.cpp
  get_dfg_node_physical_slot_for_output.cpp
  get_dfg_node_physical_slot_for_br_decision.cpp
  get_dfg_node_physical_slot_start_for_range_operand.cpp
  get_dfg_node_reg_alloc_info_codegen_func_ord.cpp
  get_end_of_call_frame_from_dfg_codeblock.cpp
  create_new_jit_generic_ic_for_dfg_jit.cpp
  get_kth_variadic_arg.cpp
  get_kth_variadic_result.cpp
  get_function_object_heap_ptr_from_stack_base.cpp
  create_closed_upvalue.cpp
  get_closed_upvalue_value.cpp
  set_closed_upvalue_value.cpp
  set_up_variadic_result_info.cpp
  move_variadic_results_for_prepend.cpp
  i64_sub_saturate_to_zero.cpp
  create_new_closure_for_dfg_and_fill_upvalues_from_parent.cpp
  box_function_object_raw_pointer_to_tvalue.cpp
  get_immutable_upvalue_from_function_obj.cpp
  get_mutable_upvalue_from_function_obj.cpp
  put_upvalue_from_function_obj.cpp
  move_variadic_results_for_return.cpp
  get_input_prediction_mask_for_simple_node.cpp
  get_input_prediction_mask_for_complex_node.cpp
  get_input_prediction_mask_range_for_complex_node.cpp
  update_type_mask_for_prediction_propagation.cpp
  get_prediction_propagation_output_range_info.cpp
  get_dfg_jit_codeblock_from_codeblock_heap_ptr.cpp
  get_num_stack_slots_in_dfg_codeblock.cpp
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

