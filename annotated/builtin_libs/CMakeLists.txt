SET(DEEGEN_USER_BUILTIN_LIB_SOURCES
  throw_error.cpp
  base_lib.cpp
  lib_math.cpp
  lib_io.cpp
  lib_debug.cpp
)

set(fps_exec "${PROJECT_BINARY_DIR}/deegen/fps")

set(generated_src_list "")
set(src_ord "0")
foreach(cur_src ${DEEGEN_USER_BUILTIN_LIB_SOURCES})
  set(lib_name "deegen_user_builtin_lib_ir_source_${src_ord}")
  
  add_library(${lib_name} OBJECT
    ${cur_src}
  )
  set_target_properties(${lib_name} PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} -DDEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY ")

  set(post_process_src "${GENERATED_FILES_DIR}/deegen_user_builtin_lib.${cur_src}.s")
  list(APPEND generated_src_list "${post_process_src}")
  set_source_files_properties(${post_process_src} PROPERTIES GENERATED true)
  
  add_custom_command(
    OUTPUT ${post_process_src}
    COMMAND ${fps_exec} --process-user-builtin-lib --ir-input=$<TARGET_OBJECTS:${lib_name}> --asm-output=${post_process_src} 
    # CMake craziness: 'DEPENDS ${lib_name}' won't work! Due to bug or whatever reason it just doesn't work for object libraries!
    # So we must write $<TARGET_OBJECTS:${lib_name}>' instead
    #
    DEPENDS fps $<TARGET_OBJECTS:${lib_name}>
  )
  
  math(EXPR src_ord "${src_ord} + 1")
endforeach()

add_library(deegen_user_builtin_lib
  ${generated_src_list}
)
set_target_properties(deegen_user_builtin_lib PROPERTIES COMPILE_FLAGS " -O3 ")

