#pragma once

#include "misc_llvm_helper.h"

namespace dast {

// A simple helper that temporarily changes a llvm::cl option, and restores it when the variable goes out of scope
// This is not thread-safe, but OK for our purpose since we only use it for our builder which is single-threaded.
//
template<typename T>
class ScopeOverrideLLVMOption
{
public:
    ScopeOverrideLLVMOption(const char* name, T value)
    {
        using namespace llvm;
        m_name = name;
        auto& optMap = cl::getRegisteredOptions();
        auto it = optMap.find(name);
        ReleaseAssert(it != optMap.end());
        m_optPtr = static_cast<cl::opt<T>*>(it->second);
        m_oldVal = *m_optPtr;
        *m_optPtr = value;
    }

    ~ScopeOverrideLLVMOption()
    {
        using namespace llvm;
        *m_optPtr = m_oldVal;
    }

private:
    const char* m_name;
    llvm::cl::opt<T>* m_optPtr;
    T m_oldVal;
};

#define ScopeOverrideLLVMOption(...) static_assert(false, "Wrong use of 'auto'-pattern!");

}   // namespace dast
