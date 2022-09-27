# For the CPP files that are intended to be compiled to IR, we want to compile them with 
# no optimizations at compilation time, so we can easily extract information from LLVM IR
# and then do the rewrites, and finally call the optimizer to do the optimizations
#
# It seems like there are two solutions to achieve this (see https://bugs.llvm.org/show_bug.cgi?id=35950 and https://github.com/klee/klee/issues/902):
# 1. use option -O0 -Xclang -disable-O0-optnone
# 2. use option -O1 -Xclang -disable-llvm-passes
# option 2 is claimed to generate more information that allows better optimization later, so we stick with option 2 for now.
#
# After some experiments with option 2, it seems like when compiled under -O1, clang automatically adds 'loop.unroll.disable' metadata
# to all the loops, which is unwanted. It seems like the '-O1' part is the problem: if we change -O1 to -O3, the problem is gone.
# After reading the original threads again, it is still unclear to me why '-O1 -Xclang -disable-llvm-passes' is proposed
# instead of '-O3 -Xclang -disable-llvm-passes', or why the '-O3' version could be problematic.
#
# So for now we use the option '-O3 -Xclang -disable-llvm-passes'.
#
# But now comes the CMake nonsense: CMake can only add flags to individual files, but cannot remove flags (without resorting to a lot of craziness)
# So here we have to strip off all relavent flags from CMAKE_CXX_FLAGS, and add them back for each library in this directory
#
string(REPLACE " -O0 " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE " -O3 " " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE ${DEBUG_OPTION} " " CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

# For CPP files to be compiled to LLVM IR, apply EXTRA_CXX_FLAGS_FOR_LLVM_IR
# "-Walloca" in EXTRA_CXX_FLAGS_FOR_LLVM_IR is required: LLVM seems to generate buggy assembly in GHC calling convention 
# if variable-sized alloca is used. It failed to recognize that rbp is garbaged by the call.
# 
SET(EXTRA_CXX_FLAGS_FOR_LLVM_IR " -emit-llvm -fno-exceptions -Walloca -O3 -Xclang -disable-llvm-passes ")

# For CPP files to be compiled normally in this directory (and subdirectory), apply EXTRA_CXX_FLAGS_FOR_NORMAL_COMPILATION
#
if(BUILD_FLAVOR STREQUAL "DEBUG")
  SET(EXTRA_CXX_FLAGS_FOR_NORMAL_COMPILATION " -O0 ${DEBUG_OPTION} ")
elseif(BUILD_FLAVOR STREQUAL "TESTREL")
  SET(EXTRA_CXX_FLAGS_FOR_NORMAL_COMPILATION " -O3 ${DEBUG_OPTION} ")
elseif(BUILD_FLAVOR STREQUAL "RELEASE")
  SET(EXTRA_CXX_FLAGS_FOR_NORMAL_COMPILATION " -O3 ")
else()
  message( FATAL_ERROR "Unknown build flavor!" )
endif()

add_subdirectory(unit_test)
add_subdirectory(deegen_common_snippets)
add_subdirectory(builtin_libs)

