cmake_minimum_required(VERSION 3.12)

# set the project name
#
project(toylang LANGUAGES C CXX ASM)

# specify the C++ compiler
#
SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
else()
  message( FATAL_ERROR "ccache not found!" )
endif()

# specify the C++ standard
#
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# build flavor
# DEBUG: test-debug build, no optimization, all assertions
# TESTREL: test-release build, optimized but with debug symbols, 'assert' disabled but 'TestAssert' enabled
# RELEASE: production build, optimized, no debug symbols, no assertions
#
set(BUILD_FLAVOR "" CACHE STRING "Build flavor, supports DEBUG/TESTREL/RELEASE")

# For some reason, our program triggers a bug in gdb 11, so we workaround by using gdb 10 for now
# However, LLVM 14 defaults to emit dwarf-5 debug info, which cannot be recognized by gdb 10 
# so insead of using '-g', we use '-gdwarf-4' to ask LLVM to emit dwarf-4 debug info
#
set(DEBUG_OPTION " -gdwarf-4 ")

if(BUILD_FLAVOR STREQUAL "DEBUG")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_FLAVOR=DEBUG -DTESTBUILD -O0 ${DEBUG_OPTION} ")
  SET(GENERATED_FILES_DIR "${PROJECT_SOURCE_DIR}/__generated__/debug/generated")
  include_directories("${PROJECT_SOURCE_DIR}/__generated__/debug")
elseif(BUILD_FLAVOR STREQUAL "TESTREL")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_FLAVOR=TESTREL -DTESTBUILD -DNDEBUG -O3 ${DEBUG_OPTION} ")
  SET(GENERATED_FILES_DIR "${PROJECT_SOURCE_DIR}/__generated__/testrel/generated")
  include_directories("${PROJECT_SOURCE_DIR}/__generated__/testrel")
elseif(BUILD_FLAVOR STREQUAL "RELEASE")
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_FLAVOR=RELEASE -DNDEBUG -O3 ")
  SET(GENERATED_FILES_DIR "${PROJECT_SOURCE_DIR}/__generated__/release/generated")
  include_directories("${PROJECT_SOURCE_DIR}/__generated__/release")
else()
  message( FATAL_ERROR "Unknown build flavor!" )
endif()

# Unfortunately CMake automatically appends uesless flags such as the include directories and the 
# -MD -MT -MF flags to the compile command for .S files, which triggers clang warning. Suppress this warning.
#
# We also add "-O3" to the command. It probably doesn't matter for assembly file, but adding it doesn't hurt 
# anything either, and Clang seems to recognize it as a valid option.
# 
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -O3 -Weverything -Wno-unused-command-line-argument ")

# For now we compile everything with PIC and PIE disabled. 
# Our copy-and-patch stencil generation logic will likely need it, 
# and it also makes the final code more efficient (e.g., interpreter dispatches can have one less memory load to load the GOT entry)
# 
# This is not strictly required: it's possible to enable PIE in our final executable 
# without sacrificing too much performance:
# 1. We can selectively disable PIE/PIC for the stencils (which doesn't get linked into the binary anyway)
# 2. We can remove unnecessary reference to the Global Offset Table by properly amalgamating the code. 
#    (The 'lea rip' overhead of PIE isn't a big problem. The GOT table overhead is since it's a memory load.)
#
# But doing so would require some more CMake work, so let's go without it for now.
#
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -fno-pic -fno-pie ")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fno-pic -fno-pie ")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -no-pie ")

# add -pthread
# 
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -pthread ")

# add -mfsgsbase, we will check support at runtime before actually using those instructions, so always having the flag is fine
#
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -mfsgsbase ")

# -rdynamic so symbols are exposed to generated code
# 
#SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -rdynamic ")

# If debug symbols are emitted, redirect them to point at the right location
# The reference to source code files always have prefix /home/u/project/ 
# (because that is our build directory in docker), so remove it
#
if((BUILD_FLAVOR STREQUAL "DEBUG") OR (BUILD_FLAVOR STREQUAL "TESTREL"))
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffile-prefix-map=/home/u/project/= ")
endif()

# gtest header
#
include_directories(SYSTEM "${PROJECT_SOURCE_DIR}/gtest/include")

# build gtest, we don't need the additional cxxflags to build gtest.
#
add_subdirectory(gtest)

# Append extra cxx warning flags
#
SET(EXTRA_WARNING_FLAGS "-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++20-compat -Wno-unused-macros -Wno-global-constructors -Wno-exit-time-destructors -Wno-weak-vtables -Wno-padded -Wno-alloca -Wno-missing-prototypes -Wno-zero-length-array -Wno-undefined-func-template -Wno-reserved-identifier -Wno-disabled-macro-expansion -Wno-gnu-zero-variadic-macro-arguments -Wno-packed -Wno-overlength-strings -Werror ")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${EXTRA_WARNING_FLAGS} ")

# llvm-specific flags
#
SET(LLVM_EXTRA_COMPILE_FLAGS "-I/usr/local/include -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS ")
execute_process(
  COMMAND bash "-c" "llvm-config --link-static --ldflags"
  OUTPUT_VARIABLE LLVM_EXTRA_LINK_FLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# TODO: only append llvm-specific flags when needed
#
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${LLVM_EXTRA_COMPILE_FLAGS} ")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${LLVM_EXTRA_LINK_FLAGS} ")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath='$ORIGIN/shared_libs' ")

# Get the llvm-specific link libraries
# There are two parts: 
# (1) the LLVM libraries (which are outputted in absolute paths)
#
execute_process(
  COMMAND bash "-c" "llvm-config --link-static --libfiles"
  OUTPUT_VARIABLE _tmp
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE " " ";" LLVM_EXTRA_LINK_LIBRARIES ${_tmp})

# (2) the system libraries. They are outputted in forms like "-lfoo -lbar". 
# We transform the output to format "foo bar", and append to LLVM_EXTRA_LINK_LIBRARIES
#
execute_process(
  COMMAND bash "-c" "llvm-config --link-static --system-libs"
  OUTPUT_VARIABLE _tmp
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE " " ";" _tmp "${_tmp}")
foreach(_loop_var ${_tmp})
  STRING(REGEX REPLACE "^-l" "" _libname ${_loop_var})
  LIST(APPEND LLVM_EXTRA_LINK_LIBRARIES ${_libname})
endforeach()

include_directories(${PROJECT_SOURCE_DIR})
include_directories("${PROJECT_SOURCE_DIR}/common_utils")
include_directories("${PROJECT_SOURCE_DIR}/toylang")
include_directories("${PROJECT_SOURCE_DIR}/deegen")

add_subdirectory(common_utils)

add_subdirectory(toylang)

add_subdirectory(deegen)

add_subdirectory(annotated)

# add the executable
#
add_executable(main
  main.cpp        
  test_boxing.cpp
  test_call_opcode.cpp
  test_global_string_conser.cpp
  test_structure.cpp
  test_object_get_put_by_id.cpp
  test_object_array_part.cpp
  test_json_bytecode_loader.cpp
  test_table_object_iterator.cpp
  test_annotation_switch_case.cpp
  test_dump_type_speculation.cpp
  test_llvm_constant_parser.cpp
  test_proven_type_specialization.cpp
  test_make_call_api_parser.cpp
  test_deegen_common_snippet_getter.cpp
  test_make_call_api_lowering.cpp
  test_return_value_accessor_api_lowering.cpp
  test_interp_function_entry_generator.cpp
  test_user_lib_func_lowering.cpp
)

# Our unit test needs LLVM JIT
# add -rdynamic so that symbols are exposed to generated code
# 
target_link_options(main PUBLIC
   -rdynamic 
)

target_link_libraries(main PUBLIC
  gtest
  -Wl,--start-group
  common_utils
  toylang
  deegen_rt
  deegen_generated
  deegen_unit_test_ir
  deegen_common_snippet_ir
  deegen_user_builtin_lib
  -Wl,--end-group
  ${LLVM_EXTRA_LINK_LIBRARIES} 
)

