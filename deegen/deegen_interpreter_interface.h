#pragma once

#include "misc_llvm_helper.h"

namespace dast {

class BytecodeVariantDefinition;

class InterpreterFunctionInterface
{
public:
    // Prepare to create the interpreter function for 'impl'.
    // This function clones the module, so the original module is untouched.
    // The cloned module is owned by this class.
    //
    InterpreterFunctionInterface(BytecodeVariantDefinition* bytecodeDef, llvm::Function* impl, bool isReturnContinuation);

    // Create wrapper logic that decodes the bytecode struct, and call 'impl'
    //
    void EmitWrapperBody();

    // Inline 'impl' into the wrapper logic, then lower APIs like 'Return', 'MakeCall', 'Error', etc.
    //
    void LowerAPIs();

    std::unique_ptr<llvm::Module> WARN_UNUSED GetResult();

    llvm::Module* GetModule() const { return m_module.get(); }
    llvm::Value* GetCoroutineCtx() const { ReleaseAssert(m_coroutineCtx != nullptr); return m_coroutineCtx; }
    llvm::Value* GetStackBase() const { ReleaseAssert(m_stackBase != nullptr); return m_stackBase; }
    llvm::Value* GetCurBytecode() const { ReleaseAssert(m_curBytecode != nullptr); return m_curBytecode; }
    llvm::Value* GetCodeBlock() const { ReleaseAssert(m_codeBlock != nullptr); return m_codeBlock; }
    llvm::Value* GetRetStart() const { ReleaseAssert(m_retStart != nullptr && m_isReturnContinuation); return m_retStart; }
    llvm::Value* GetNumRet() const { ReleaseAssert(m_numRet != nullptr && m_isReturnContinuation); return m_numRet; }

    void InvalidateContextValues()
    {
        m_coroutineCtx = nullptr;
        m_stackBase = nullptr;
        m_curBytecode = nullptr;
        m_codeBlock = nullptr;
        m_retStart = nullptr;
        m_numRet = nullptr;
    }

    std::unique_ptr<llvm::Module> WARN_UNUSED ProcessReturnContinuation(llvm::Function* rc);

    static llvm::FunctionType* WARN_UNUSED GetInterfaceFunctionType(llvm::LLVMContext& ctx, bool forReturnContinuation);

private:
    BytecodeVariantDefinition* m_bytecodeDef;

    std::unique_ptr<llvm::Module> m_module;
    // The return continuation additionally has access to 'TValue* retStart' and 'size_t numRets'
    //
    bool m_isReturnContinuation;

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

    bool m_didEmitWrapper;
    bool m_didLowerAPIs;
    bool m_didGetResult;

    llvm::Value* m_coroutineCtx;
    llvm::Value* m_stackBase;
    llvm::Value* m_curBytecode;
    llvm::Value* m_codeBlock;
    llvm::Value* m_retStart;
    llvm::Value* m_numRet;
};

}   // namespace dast
