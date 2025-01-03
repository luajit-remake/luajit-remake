#pragma once

// Including this file disables debug and test assertions, even if CMake is specifying a different build mode.
// This utility is only supposed to be used by deegen_common_snippets.
//
// This file must be included at the beginning of a CPP file.
//
// Since each common snippet is extracted into a piece of LLVM IR, and everything else is discarded, we don't
// need to worry about the no-assertion versions of the header file functions getting "leaked" into the executable.
//
// Note that this does not affect optimization levels and debug symbols. CMake is still responsible for change that.
//

#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION) || defined(_CPPLIB_VER) || defined(__GLIBC__)
#error "This file must be included before any standard libraries!"
#endif

#if defined(Assert) || defined(TestAssert)
#error "This file must be included as the first header file!"
#endif

// Note that we must not change the build mode, since structs may include TESTBUILD_ONLY/DEBUG_ONLY members,
// thus have different layouts in different build modes!
// So changing the build mode can cause silent memory corruptions!
//
#define DISABLE_DEBUG_ASSERT
#define DISABLE_TEST_ASSERT
