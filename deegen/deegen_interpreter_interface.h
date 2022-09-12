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

    // Inline 'impl' into the wrapper logic, then lower APIs like 'Return', 'MakeCall', 'Error', etc.
    //
    void LowerAPIs();

    llvm::Module* GetModule() const { return m_module.get(); }
    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtxIdent); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBaseIdent); }
    llvm::Value* GetCurBytecode() const { return m_valuePreserver.Get(x_curBytecodeIdent); }
    llvm::Value* GetCodeBlock() const { return m_valuePreserver.Get(x_codeBlockIdent); }
    llvm::Value* GetRetStart() const { return m_valuePreserver.Get(x_retStartIdent); }
    llvm::Value* GetNumRet() const { return m_valuePreserver.Get(x_numRetIdent); }

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

    LLVMValuePreserver m_valuePreserver;
    bool m_didLowerAPIs;

    static constexpr const char* x_coroutineCtxIdent = "coroutineCtx";
    static constexpr const char* x_stackBaseIdent = "stackBase";
    static constexpr const char* x_curBytecodeIdent = "curBytecode";
    static constexpr const char* x_codeBlockIdent = "codeBlock";
    static constexpr const char* x_retStartIdent = "retStart";
    static constexpr const char* x_numRetIdent = "numRet";
};

}   // namespace dast
