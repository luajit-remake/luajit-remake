SET(DEEGEN_BYTECODE_DEFINITION_SOURCES
  arithmetic_bytecodes.cpp
  comparison_bytecodes.cpp
  call_bytecodes.cpp
  global_get.cpp
  global_put.cpp
  return_bytecodes.cpp
  bytecode_mov.cpp
  unconditional_branch.cpp
  set_constant_value.cpp
  new_closure.cpp
  table_dup.cpp
  table_get_by_id.cpp
  table_get_by_val.cpp
  table_put_by_id.cpp
  table_put_by_val.cpp
  table_get_by_imm.cpp
  table_put_by_imm.cpp
  unary_minus.cpp
  table_new.cpp
  upvalue_bytecodes.cpp
  numeric_for_loop.cpp
  equality_bytecodes.cpp
  test_and_branch_bytecodes.cpp
  logical_not.cpp
  length_operator.cpp
  variadic_args_accessor.cpp
  range_fill_nils.cpp
  concat.cpp
  table_variadic_put_by_seq.cpp
  iterative_for_loop.cpp
  iterative_for_loop_kv.cpp
  tail_call_bytecodes.cpp
)

set(fps_exec "${PROJECT_BINARY_DIR}/deegen/fps")

# generate the interpreter 
#
set(generated_json_list "")
set(src_ord "0")
foreach(cur_src ${DEEGEN_BYTECODE_DEFINITION_SOURCES})
  set(lib_name "deegen_bytecode_definition_ir_source_${src_ord}")
  
  add_library(${lib_name} OBJECT
    ${cur_src}
  )
  set_target_properties(${lib_name} PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} -DDEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION ")

  set(post_process_json "${GENERATED_FILES_DIR}/bytecode_info.interpreter.${cur_src}.json")
  string(SHA1 cur_src_hash "${cur_src}")
  set(post_process_hdr "${GENERATED_FILES_DIR}/deegen_bytecode_builder_defs.${cur_src_hash}.h")
  list(APPEND generated_json_list "${post_process_json}")
  set_source_files_properties(${post_process_json} PROPERTIES GENERATED true)
  set_source_files_properties(${post_process_hdr} PROPERTIES GENERATED true)
  
  add_custom_command(
    OUTPUT ${post_process_json}
    OUTPUT ${post_process_hdr}
    COMMAND ${fps_exec} --process-bytecode-definition-for-interpreter --ir-input=$<TARGET_OBJECTS:${lib_name}> --json-output=${post_process_json} --hdr-output=${post_process_hdr} --audit-dir=${GENERATED_FILES_DIR}/../audit
    DEPENDS fps $<TARGET_OBJECTS:${lib_name}>
  )
  
  math(EXPR src_ord "${src_ord} + 1")
endforeach()

set(bytecode_builder_api_header "${GENERATED_FILES_DIR}/all_bytecode_builder_apis.h")
set(interpreter_dispatch_table_builder "${GENERATED_FILES_DIR}/build_interpreter_dispatch_table.cpp")
set(interpreter_opcode_name_table_builder "${GENERATED_FILES_DIR}/build_interpreter_opcode_name_table.cpp")
set_source_files_properties(${bytecode_builder_api_header} PROPERTIES GENERATED true)
set_source_files_properties(${interpreter_dispatch_table_builder} PROPERTIES GENERATED true)
set_source_files_properties(${interpreter_opcode_name_table_builder} PROPERTIES GENERATED true)
list(JOIN generated_json_list "," comma_separated_json_file_list)
add_custom_command(
  OUTPUT ${bytecode_builder_api_header}
  OUTPUT ${interpreter_dispatch_table_builder}
  OUTPUT ${interpreter_opcode_name_table_builder}
  COMMAND ${fps_exec} --generate-bytecode-builder-api-header --input-list='${comma_separated_json_file_list}' --hdr-output=${bytecode_builder_api_header} --cpp-output=${interpreter_dispatch_table_builder} --cpp-output-2=${interpreter_opcode_name_table_builder}
  DEPENDS fps ${generated_json_list} 
)

# generate the interpreter dispatch table 
#
add_executable(deegen_generate_interpreter_opcode_name_table_json
  ${interpreter_opcode_name_table_builder}
)

set(interpreter_opcode_name_table_builder_exec "${PROJECT_BINARY_DIR}/annotated/bytecodes/deegen_generate_interpreter_opcode_name_table_json")

set(interpreter_opcode_name_table_json "${GENERATED_FILES_DIR}/interpreter_opcode_names.json")
set_source_files_properties(${interpreter_opcode_name_table_json} PROPERTIES GENERATED true)
add_custom_command(
  OUTPUT ${interpreter_opcode_name_table_json}
  COMMAND ${interpreter_opcode_name_table_builder_exec} ${interpreter_opcode_name_table_json}
  DEPENDS deegen_generate_interpreter_opcode_name_table_json
)

# generate the baseline jit
#
set(baseline_jit_dispatch_table_hdr_file_list "")
set(generated_src_list "")
foreach(cur_src ${DEEGEN_BYTECODE_DEFINITION_SOURCES})
  set(post_process_src "${GENERATED_FILES_DIR}/deegen_bytecode_defs.${cur_src}.s")
  set(input_json "${GENERATED_FILES_DIR}/bytecode_info.interpreter.${cur_src}.json")

  string(SHA1 cur_src_hash "${cur_src}")
  set(post_process_hdr "${GENERATED_FILES_DIR}/deegen_baseline_jit_bytecode_traits.${cur_src_hash}.h")
  set_source_files_properties(${post_process_hdr} PROPERTIES GENERATED true)
  list(APPEND baseline_jit_dispatch_table_hdr_file_list "${post_process_hdr}")
  
  list(APPEND generated_src_list "${post_process_src}")
  set_source_files_properties(${post_process_src} PROPERTIES GENERATED true)

  add_custom_command(
    OUTPUT ${post_process_src}
    OUTPUT ${post_process_hdr}
    COMMAND ${fps_exec} --process-bytecode-definition-for-baseline-jit --bytecode-name-table=${interpreter_opcode_name_table_json} --json-input=${input_json} --asm-output=${post_process_src} --hdr-output=${post_process_hdr} --audit-dir=${GENERATED_FILES_DIR}/../audit 
    DEPENDS fps ${interpreter_opcode_name_table_json} ${input_json}
  )
endforeach()

# generate the baseline JIT codegen function dispatch table and bytecode trait table
#
set(baseline_jit_dispatch_and_trait_table_cpp "${GENERATED_FILES_DIR}/build_baseline_jit_dispatch_and_trait_table.cpp")
set_source_files_properties(${baseline_jit_dispatch_and_trait_table_cpp} PROPERTIES GENERATED true)
list(JOIN baseline_jit_dispatch_table_hdr_file_list "," comma_separated_baseline_jit_dispatch_table_hdr_file_list)
add_custom_command(
  OUTPUT ${baseline_jit_dispatch_and_trait_table_cpp}
  COMMAND ${fps_exec} --generate-baseline-jit-dispatch-and-bytecode-trait-table --input-list='${comma_separated_baseline_jit_dispatch_table_hdr_file_list}' --cpp-output=${baseline_jit_dispatch_and_trait_table_cpp} --bytecode-name-table=${interpreter_opcode_name_table_json}
  DEPENDS fps ${generated_src_list} ${baseline_jit_dispatch_table_hdr_file_list} ${interpreter_opcode_name_table_json}
)

add_library(deegen_generated_interpreter_impl OBJECT
  ${generated_src_list}
  ${interpreter_dispatch_table_builder}
  ${baseline_jit_dispatch_and_trait_table_cpp}
)
set_target_properties(deegen_generated_interpreter_impl PROPERTIES COMPILE_FLAGS " -O3 ")

