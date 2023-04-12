#pragma once

#include "common.h"

namespace dast {

// A simple helper function that compiles ASM (.s) file to object file (.o) using clang -O3
// Return the file contents of the object file as a string
//
std::string WARN_UNUSED CompileAssemblyFileToObjectFile(const std::string& asmFileContents, const std::string& extraCmdlineArgs);

// Compile a CPP file to object file or LLVM IR file using clang -O3
// If 'storePath' is provided, the file and compilation result will be stored there.
// Return the compilation result file contents as a string
//
std::string WARN_UNUSED CompileCppFileToObjectFile(const std::string& cppFileContents, const std::string& storePath = "");
std::string WARN_UNUSED CompileCppFileToLLVMBitcode(const std::string& cppFileContents, const std::string& storePath = "");

}   // namespace dast
