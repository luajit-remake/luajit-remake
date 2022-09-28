SET(DEEGEN_BYTECODE_DEFINITION_SOURCES
  arithmetic_bytecodes.cpp
)

set(fps_exec "${PROJECT_BINARY_DIR}/deegen/fps")

set(generated_src_list "")
set(src_ord "0")
foreach(cur_src ${DEEGEN_BYTECODE_DEFINITION_SOURCES})
  set(lib_name "deegen_bytecode_definition_ir_source_${src_ord}")
  
  add_library(${lib_name} OBJECT
    ${cur_src}
  )
  set_target_properties(${lib_name} PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} -DDEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION ")

  set(post_process_src "${GENERATED_FILES_DIR}/deegen_bytecode_defs.${cur_src}.s")
  list(APPEND generated_src_list "${post_process_src}")
  set_source_files_properties(${post_process_src} PROPERTIES GENERATED true)
  
  add_custom_command(
    OUTPUT ${post_process_src}
    COMMAND ${fps_exec} --process-bytecode-definition-for-interpreter --ir-input=$<TARGET_OBJECTS:${lib_name}> --asm-output=${post_process_src} 
    DEPENDS fps $<TARGET_OBJECTS:${lib_name}>
  )
  
  math(EXPR src_ord "${src_ord} + 1")
endforeach()

add_library(deegen_generated_interpreter_impl
  ${generated_src_list}
)
set_target_properties(deegen_generated_interpreter_impl PROPERTIES COMPILE_FLAGS " -O3 ")

