#pragma once

// The purpose of this header file is to force the translation unit to be built with RELEASE flags,
// even if CMake is specifying a different build mode.
//
// This file must only be included at the beginning of a CPP file, before anything else is
// included (including standard libraries). This is because the NDEBUG flag (which is one
// of the macros we need to modify) affects standard libraries.
//
// Note that this does not affect optimization levels and debug symbols. CMake is still responsible for change that.
//
// The main users of this file are the CPP files to be compiled to LLVM IR, where we often don't want to have
// different IR in different build modes.
//

#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION) || defined(_CPPLIB_VER) || defined(__GLIBC__)
#error "This file must be included before any standard libraries!"
#endif

#ifndef NDEBUG
#define NDEBUG
#endif

#ifdef TESTBUILD
#undef TESTBUILD
#endif

#ifdef BUILD_FLAVOR
#undef BUILD_FLAVOR
#endif

#define BUILD_FLAVOR RELEASE
