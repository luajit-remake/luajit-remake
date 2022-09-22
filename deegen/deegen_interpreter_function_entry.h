#pragma once

#include "misc_llvm_helper.h"

namespace dast {

class InterpreterFunctionEntryLogicCreator
{
public:
    // numSpecializedFixedParams == -1 means it's not specialized
    //
    InterpreterFunctionEntryLogicCreator(bool acceptVarArgs, size_t numSpecializedFixedParams)
        : m_generated(false)
        , m_acceptVarArgs(acceptVarArgs)
        , m_numSpecializedFixedParams(numSpecializedFixedParams)
    { }

    std::unique_ptr<llvm::Module> WARN_UNUSED Get(llvm::LLVMContext& ctx);
    std::string GetFunctionName();

    bool IsNumFixedParamSpecialized() { return m_numSpecializedFixedParams != static_cast<size_t>(-1); }
    size_t GetSpecializedNumFixedParam() { ReleaseAssert(IsNumFixedParamSpecialized()); return m_numSpecializedFixedParams; }

private:
    bool m_generated;
    bool m_acceptVarArgs;
    size_t m_numSpecializedFixedParams;
};

}   // namespace dast
