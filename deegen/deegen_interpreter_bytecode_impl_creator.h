#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"

namespace dast {

class BytecodeVariantDefinition;

class InterpreterBytecodeImplCreator
{
public:
    // This clones the input module, so that the input module is untouched
    //
    InterpreterBytecodeImplCreator(BytecodeIrComponent& bic);

    // Inline 'impl' into the wrapper logic, then lower APIs like 'Return', 'MakeCall', 'Error', etc.
    //
    void DoLowering();

    static std::unique_ptr<InterpreterBytecodeImplCreator> WARN_UNUSED LowerOneComponent(BytecodeIrComponent& bic);
    static std::unique_ptr<llvm::Module> WARN_UNUSED DoLoweringForAll(BytecodeIrInfo& bi);

    bool IsReturnContinuation() const { return m_processKind == BytecodeIrComponentKind::ReturnContinuation; }
    BytecodeVariantDefinition* GetBytecodeDef() const { return m_bytecodeDef; }
    llvm::Module* GetModule() const { return m_module.get(); }
    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtx); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBase); }
    llvm::Value* GetCurBytecode() const { return m_valuePreserver.Get(x_curBytecode); }
    llvm::Value* GetCodeBlock() const { return m_valuePreserver.Get(x_codeBlock); }
    llvm::Value* GetRetStart() const { ReleaseAssert(IsReturnContinuation()); return m_valuePreserver.Get(x_retStart); }
    llvm::Value* GetNumRet() const { ReleaseAssert(IsReturnContinuation()); return m_valuePreserver.Get(x_numRet); }
    llvm::Value* GetOutputSlot() const { return m_valuePreserver.Get(x_outputSlot); }
    llvm::Value* GetCondBrDest() const { return m_valuePreserver.Get(x_condBrDest); }
    llvm::Value* GetBytecodeMetadataPtr() const { return m_valuePreserver.Get(x_metadataPtr); }

    llvm::CallInst* CallDeegenCommonSnippet(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenCommonSnippet(GetModule(), dcsName, args, insertBefore);
    }

    llvm::CallInst* CallDeegenRuntimeFunction(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenRuntimeFunction(GetModule(), dcsName, args, insertBefore);
    }

    std::string WARN_UNUSED GetResultFunctionName() { return m_resultFuncName; }

    static constexpr const char* x_hot_code_section_name = "deegen_interpreter_code_section_hot";
    static constexpr const char* x_cold_code_section_name = "deegen_interpreter_code_section_cold";

private:
    void CreateWrapperFunction();
    void LowerGetBytecodeMetadataPtrAPI();

    BytecodeVariantDefinition* m_bytecodeDef;

    std::unique_ptr<llvm::Module> m_module;
    BytecodeIrComponentKind m_processKind;

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

    // The name of the wrapper function (i.e., the final product)
    //
    std::string m_resultFuncName;

    LLVMValuePreserver m_valuePreserver;
    bool m_generated;

    static constexpr const char* x_coroutineCtx = "coroutineCtx";
    static constexpr const char* x_stackBase = "stackBase";
    static constexpr const char* x_curBytecode = "curBytecode";
    static constexpr const char* x_codeBlock = "codeBlock";
    static constexpr const char* x_retStart = "retStart";
    static constexpr const char* x_numRet = "numRet";
    static constexpr const char* x_outputSlot = "outputSlot";
    static constexpr const char* x_condBrDest = "condBrDest";
    static constexpr const char* x_metadataPtr = "metadataPtr";
};

}   // namespace dast
