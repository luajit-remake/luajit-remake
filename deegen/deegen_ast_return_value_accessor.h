#pragma once

#include "misc_llvm_helper.h"

namespace dast {

class DeegenBytecodeImplCreatorBase;
class InterpreterBytecodeImplCreator;

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

    void DoLoweringForInterpreterOrBaselineOrDfg(DeegenBytecodeImplCreatorBase* ifi);

    static void LowerForInterpreterOrBaselineOrDfg(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func)
    {
        std::vector<AstReturnValueAccessor> res = GetAllUseInFunction(func);
        for (AstReturnValueAccessor& item : res)
        {
            item.DoLoweringForInterpreterOrBaselineOrDfg(ifi);
        }
    }

    Kind m_kind;
    llvm::CallInst* m_origin;
};

}   // namespace dast
