#pragma once

#include "misc_llvm_helper.h"

namespace dast {

class BytecodeVariantDefinition;

class InterpreterBytecodeImplCreator
{
public:
    // Prepare to create the interpreter function for 'impl'.
    // This function clones the module, so the original module is untouched.
    // The cloned module is owned by this class.
    //
    InterpreterBytecodeImplCreator(BytecodeVariantDefinition* bytecodeDef, llvm::Function* impl, bool isReturnContinuation);

    // Inline 'impl' into the wrapper logic, then lower APIs like 'Return', 'MakeCall', 'Error', etc.
    //
    std::unique_ptr<llvm::Module> WARN_UNUSED Get();

    bool IsReturnContinuation() const { return m_isReturnContinuation; }
    BytecodeVariantDefinition* GetBytecodeDef() const { return m_bytecodeDef; }
    llvm::Module* GetModule() const { return m_module.get(); }
    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtx); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBase); }
    llvm::Value* GetCurBytecode() const { return m_valuePreserver.Get(x_curBytecode); }
    llvm::Value* GetCodeBlock() const { return m_valuePreserver.Get(x_codeBlock); }
    llvm::Value* GetRetStart() const { ReleaseAssert(m_isReturnContinuation); return m_valuePreserver.Get(x_retStart); }
    llvm::Value* GetNumRet() const { ReleaseAssert(m_isReturnContinuation); return m_valuePreserver.Get(x_numRet); }
    llvm::Value* GetOutputSlot() const { return m_valuePreserver.Get(x_outputSlot); }
    llvm::Value* GetCondBrDest() const { return m_valuePreserver.Get(x_condBrDest); }

    llvm::CallInst* CallDeegenCommonSnippet(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenCommonSnippet(GetModule(), dcsName, args, insertBefore);
    }

    llvm::CallInst* CallDeegenRuntimeFunction(const std::string& dcsName, llvm::ArrayRef<llvm::Value*> args, llvm::Instruction* insertBefore)
    {
        return CreateCallToDeegenRuntimeFunction(GetModule(), dcsName, args, insertBefore);
    }

    std::unique_ptr<llvm::Module> WARN_UNUSED ProcessReturnContinuation(llvm::Function* rc);

private:
    BytecodeVariantDefinition* m_bytecodeDef;

    std::unique_ptr<llvm::Module> m_module;
    // The return continuation additionally has access to 'TValue* retStart' and 'size_t numRets'
    //
    bool m_isReturnContinuation;

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

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
};

}   // namespace dast
