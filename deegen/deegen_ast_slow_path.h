#pragma once

#include "common_utils.h"
#include "misc_llvm_helper.h"

namespace dast {

class DeegenBytecodeImplCreatorBase;
class InterpreterBytecodeImplCreator;
struct BytecodeIrComponent;

class AstSlowPath
{
public:
    static void PreprocessModule(llvm::Module* module);
    static void LowerAllForInterpreterOrBaselineOrDfg(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func);
    static std::vector<AstSlowPath> GetAllUseInFunction(llvm::Function* func);

    void CheckWellFormedness(llvm::Function* bytecodeImplFunc);
    void LowerForInterpreterOrBaselineOrDfg(DeegenBytecodeImplCreatorBase* ifi, llvm::Function* func);

    llvm::Function* WARN_UNUSED GetImplFunction();
    static std::string WARN_UNUSED GetPostProcessSlowPathFunctionNameForInterpreter(llvm::Function* implFunc);

    static std::vector<llvm::Value*> WARN_UNUSED CreateCallArgsInSlowPathWrapperFunction(uint32_t extraArgsBegin, llvm::Function* implFunc, llvm::BasicBlock* bb);

    // For the DFG SaveRegStub wrapper of an AOT slow path,
    // return the map from the DFG SlowPathArgTempBuffer to the arguments passed to the slow path.
    //
    // Specifically, suppose this function returns vector 'v',
    // then the value stored in tempBuffer[i] should be passed to argument ordinal v[i] in the AOT slow path function interface.
    //
    static std::vector<uint64_t> WARN_UNUSED GetDfgCallArgMapInSaveRegStub(BytecodeIrComponent& bic);

    llvm::CallInst* m_origin;
};

}   // namespace dast
