#pragma once

#include "misc_llvm_helper.h"

namespace dast {

class InterpreterFunctionInterface;

class AstReturnValueAccessor
{
public:
    enum class Kind
    {
        GetOneReturnValue,
        GetNumReturns,
        StoreFirstKFillNil,
        StoreAsVariadicResults
    };

    AstReturnValueAccessor(Kind kind, llvm::CallInst* origin);

    static std::vector<AstReturnValueAccessor> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    void DoLoweringForInterpreter(InterpreterFunctionInterface* ifi);

    static void LowerForInterpreter(InterpreterFunctionInterface* ifi, llvm::Function* func)
    {
        std::vector<AstReturnValueAccessor> res = GetAllUseInFunction(func);
        for (AstReturnValueAccessor& item : res)
        {
            item.DoLoweringForInterpreter(ifi);
        }
    }

    Kind m_kind;
    llvm::CallInst* m_origin;
};

}   // namespace dast
