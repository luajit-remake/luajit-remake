# always compile with RELEASE in this directory, otherwise it's going to cause 
# a lot of problems to our unit tests due to different IR in different builds
#
# This is really ugly, but let's not bother too much for now
#
string(REPLACE " -DBUILD_FLAVOR=DEBUG " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE " -DBUILD_FLAVOR=TESTREL " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE " -DBUILD_FLAVOR=RELEASE " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE " -DTESTBUILD " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE " -DNDEBUG " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_FLAVOR=RELEASE -DNDEBUG ")

SET(DEEGEN_COMMON_SNIPPET_IR_SOURCES
  get_codeblock_from_stack_header.cpp
  get_bytecode_ptr_after_return_from_call.cpp
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

