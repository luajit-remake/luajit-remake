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
)

set(fps_exec "${PROJECT_BINARY_DIR}/deegen/fps")

set(generated_src_list "")
set(generated_json_list "")
set(src_ord "0")
foreach(cur_src ${DEEGEN_BYTECODE_DEFINITION_SOURCES})
  set(lib_name "deegen_bytecode_definition_ir_source_${src_ord}")
  
  add_library(${lib_name} OBJECT
    ${cur_src}
  )
  set_target_properties(${lib_name} PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} -DDEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION ")

  set(post_process_src "${GENERATED_FILES_DIR}/deegen_bytecode_defs.${cur_src}.s")
  set(post_process_json "${GENERATED_FILES_DIR}/deegen_bytecode_builder_defs.${cur_src}.json")
  string(SHA1 cur_src_hash "${cur_src}")
  set(post_process_hdr "${GENERATED_FILES_DIR}/deegen_bytecode_builder_defs.${cur_src_hash}.h")
  list(APPEND generated_src_list "${post_process_src}")
  list(APPEND generated_json_list "${post_process_json}")
  set_source_files_properties(${post_process_src} PROPERTIES GENERATED true)
  set_source_files_properties(${post_process_json} PROPERTIES GENERATED true)
  set_source_files_properties(${post_process_hdr} PROPERTIES GENERATED true)
  
  add_custom_command(
    OUTPUT ${post_process_src}
    OUTPUT ${post_process_json}
    COMMAND ${fps_exec} --process-bytecode-definition-for-interpreter --ir-input=$<TARGET_OBJECTS:${lib_name}> --asm-output=${post_process_src} --json-output=${post_process_json} --hdr-output=${post_process_hdr}
    DEPENDS fps $<TARGET_OBJECTS:${lib_name}>
  )
  
  math(EXPR src_ord "${src_ord} + 1")
endforeach()

set(bytecode_builder_api_header "${GENERATED_FILES_DIR}/all_bytecode_builder_apis.h")
set(interpreter_dispatch_table_builder "${GENERATED_FILES_DIR}/build_interpreter_dispatch_table.cpp")
set_source_files_properties(${bytecode_builder_api_header} PROPERTIES GENERATED true)
set_source_files_properties(${interpreter_dispatch_table_builder} PROPERTIES GENERATED true)
list(JOIN generated_json_list "," comma_separated_json_file_list)
add_custom_command(
  OUTPUT ${bytecode_builder_api_header}
  OUTPUT ${interpreter_dispatch_table_builder}
  COMMAND ${fps_exec} --generate-bytecode-builder-api-header --input-list='${comma_separated_json_file_list}' --hdr-output=${bytecode_builder_api_header} --cpp-output=${interpreter_dispatch_table_builder}
  DEPENDS fps ${generated_json_list} ${generated_src_list}
)

add_library(deegen_generated_interpreter_impl OBJECT
  ${generated_src_list}
  ${interpreter_dispatch_table_builder}
)
set_target_properties(deegen_generated_interpreter_impl PROPERTIES COMPILE_FLAGS " -O3 ")

