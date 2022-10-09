#pragma once

#include "common.h"
#include "llvm/IR/Function.h"
#include "llvm/Demangle/Demangle.h"

inline bool WARN_UNUSED IsCXXSymbol(const std::string& symbol)
{
    return symbol.starts_with("_Z");
}

inline std::string WARN_UNUSED DemangleCXXSymbol(const std::string& symbol)
{
    // https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
    //
    int status = -100;
    char* res = llvm::itaniumDemangle(symbol.c_str(), nullptr, nullptr, &status /*out*/);
    if (status != llvm::demangle_success)
    {
        fprintf(stderr, "[ERROR] Attempt to demangle CXX symbol '%s' failed with status %d.\n",
                symbol.c_str(), status);
        abort();
    }
    std::string s = res;
    // 'the demangled name is placed in a region of memory allocated with malloc', so we need to use 'free' to delete it
    //
    free(res);
    return s;
}

// Get the demangled name of 'func'
// 'func' must be a C++ function
//
inline std::string WARN_UNUSED GetDemangledName(llvm::Function* func)
{
    std::string funcName = func->getName().str();
    ReleaseAssert(IsCXXSymbol(funcName));
    return DemangleCXXSymbol(funcName);
}
