# The files in this directory are compiled to IR and then packaged into a resource library
# so the unit test can access those IR.
# The actual unit tests do not reside in this directory. 
# Only the CPP files to be compiled to LLVM IR are in this directory.
#
SET(DEEGEN_UNIT_TEST_IR_SOURCES
  switch_case.cpp
  bytecode_definition_api.cpp
  llvm_constant_parser.cpp
  proven_type_specialization.cpp
  make_call_api_parser.cpp
  bytecode_interpreter_lowering_sanity_1.cpp
  make_call_api_lowering.cpp
  return_value_accessor_api_lowering.cpp
  user_lib_func_api.cpp
)

add_library(deegen_unit_test_ir_sources OBJECT
  ${DEEGEN_UNIT_TEST_IR_SOURCES}
)
set_target_properties(deegen_unit_test_ir_sources PROPERTIES COMPILE_FLAGS " ${EXTRA_CXX_FLAGS_FOR_LLVM_IR} -DDEEGEN_ANNOTATED_SOURCE_FOR_USER_BUILTIN_LIBRARY -DDEEGEN_ANNOTATED_SOURCE_FOR_BYTECODE_DEFINITION")

# This is the logic that packages the IR into the resource library to be linked to the unit test executable
# This is a really simple file but it needs to process a lot of data, so always compile with -O3 for speed
#
add_executable(package_unit_test_ir
  package_unit_test_ir.cpp   
) 
set_target_properties(package_unit_test_ir PROPERTIES COMPILE_FLAGS " -O3 ")

set(generated_src_list "")
foreach(cur_src ${DEEGEN_UNIT_TEST_IR_SOURCES})
  set(cur_generated_src "${GENERATED_FILES_DIR}/deegen_unittest.${cur_src}.ir.cpp")
  list(APPEND generated_src_list "${cur_generated_src}")
  set_source_files_properties(${cur_generated_src} PROPERTIES GENERATED true)
endforeach()

set_source_files_properties(${GENERATED_FILES_DIR}/deegen_unit_test_ir_accessor.cpp PROPERTIES GENERATED true)

add_custom_command(
  OUTPUT ${GENERATED_FILES_DIR}/deegen_unit_test_ir_accessor.cpp
  OUTPUT ${generated_src_list}
  COMMAND ${PROJECT_BINARY_DIR}/annotated/unit_test/package_unit_test_ir '$<TARGET_OBJECTS:deegen_unit_test_ir_sources>' "${GENERATED_FILES_DIR}"
  DEPENDS $<TARGET_OBJECTS:deegen_unit_test_ir_sources> package_unit_test_ir 
)

add_library(deegen_unit_test_ir 
  ${GENERATED_FILES_DIR}/deegen_unit_test_ir_accessor.cpp
  ${generated_src_list}
)
set_target_properties(deegen_unit_test_ir PROPERTIES COMPILE_FLAGS " -O3 ")

