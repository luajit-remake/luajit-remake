#pragma once

#include <cxxabi.h>

#include "common.h"
#include "llvm/IR/Function.h"

bool WARN_UNUSED IsCXXSymbol(const std::string& symbol)
{
    return symbol.starts_with("_Z");
}

std::string WARN_UNUSED DemangleCXXSymbol(const std::string& symbol)
{
    // https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
    //
    int status = -100;
    char* res = abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status /*out*/);
    ReleaseAssert(status == 0);
    std::string s = res;
    // 'the demangled name is placed in a region of memory allocated with malloc', so we need to use 'free' to delete it
    //
    free(res);
    return s;
}

// Get the demangled name of 'func'
// 'func' must be a C++ function
//
std::string WARN_UNUSED GetDemangledName(llvm::Function* func)
{
    std::string funcName = func->getName().str();
    ReleaseAssert(IsCXXSymbol(funcName));
    return DemangleCXXSymbol(funcName);
}
