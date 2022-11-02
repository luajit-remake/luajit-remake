#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

class InterpreterBytecodeImplCreator;

class AstSlowPath
{
public:
    static void PreprocessModule(llvm::Module* module);
    static void LowerAllForInterpreter(InterpreterBytecodeImplCreator* ifi, llvm::Function* func);
    static std::vector<AstSlowPath> GetAllUseInFunction(llvm::Function* func);

    void CheckWellFormedness(llvm::Function* bytecodeImplFunc);
    void LowerForInterpreter(InterpreterBytecodeImplCreator* ifi);

    llvm::Function* WARN_UNUSED GetImplFunction();
    static std::string WARN_UNUSED GetPostProcessSlowPathFunctionNameForInterpreter(llvm::Function* implFunc);

    static std::vector<llvm::Value*> WARN_UNUSED CreateCallArgsInSlowPathWrapperFunction(uint32_t extraArgsBegin, llvm::Function* implFunc, llvm::BasicBlock* bb);

    llvm::CallInst* m_origin;
};

}   // namespace dast
