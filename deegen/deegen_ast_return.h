#pragma once

#include "common.h"
#include "misc_llvm_helper.h"
#include "cxx_symbol_demangler.h"
#include "deegen_api.h"

namespace dast {

class InterpreterFunctionInterface;

class AstBytecodeReturn
{
public:
    bool HasValue() const { return m_valueOperand != nullptr; }
    llvm::Value* ValueOperand() const { ReleaseAssert(HasValue()); return m_valueOperand; }

    static std::vector<AstBytecodeReturn> WARN_UNUSED GetAllUseInFunction(llvm::Function* func);

    void DoLoweringForInterpreter(InterpreterFunctionInterface* ifi);

    llvm::CallInst* m_origin;
    // Whether this is a ReturnAndBranch API call
    //
    bool m_doesBranch;
    // nullptr if this returns nothing
    //
    llvm::Value* m_valueOperand;
};

}   // namespace dast
