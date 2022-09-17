SET(DEEGEN_COMMON_SNIPPET_IR_SOURCES
  get_codeblock_from_stack_header.cpp
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
)

add_library(deegen_common_snippet_ir_sources OBJECT
  ${DEEGEN_COMMON_SNIPPET_IR_SOURCES}
)
set_target_properties(deegen_common_snippet_ir_sources PROPERTIES COMPILE_FLAGS ${EXTRA_CXX_FLAGS_FOR_LLVM_IR})

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

