#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "deegen_api.h"

namespace dast {

class InterpreterBytecodeImplCreator;

class AstBytecodeReturn
{
public:
    bool DoesBranch() const { return m_doesBranch; }
    bool HasValueOutput() const { return m_valueOperand != nullptr; }
    llvm::Value* ValueOperand() const { ReleaseAssert(HasValueOutput()); return m_valueOperand; }

    static std::vector<AstBytecodeReturn> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    void DoLoweringForInterpreter(InterpreterBytecodeImplCreator* ifi);

    static void LowerForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func)
    {
        std::vector<AstBytecodeReturn> res = GetAllUseInFunction(func);
        for (AstBytecodeReturn& item : res)
        {
            item.DoLoweringForInterpreter(ifi);
        }
    }

    llvm::CallInst* m_origin;
    // Whether this is a ReturnAndBranch API call
    //
    bool m_doesBranch;
    // nullptr if this returns nothing
    //
    llvm::Value* m_valueOperand;
};

}   // namespace dast
