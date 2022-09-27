#pragma once

#include "misc_llvm_helper.h"

namespace dast
{

class DeegenLibFuncInstance
{
    MAKE_NONCOPYABLE(DeegenLibFuncInstance);
    MAKE_NONMOVABLE(DeegenLibFuncInstance);

public:
    // Note that the input 'target' is the dummy declaration, and this constructor will change
    // it to the true definition, and as a result, any pointer to 'target' will be invalidated
    //
    DeegenLibFuncInstance(llvm::Function* impl, llvm::Function* target, bool isRc);

    // This function should be called after a PerFunctionSimplifyOnly desugaring pass on the module
    //
    void DoLowering();

    llvm::Module* GetModule() const { return m_module; }
    llvm::Value* GetCoroutineCtx() const { return m_valuePreserver.Get(x_coroutineCtx); }
    llvm::Value* GetStackBase() const { return m_valuePreserver.Get(x_stackBase); }
    llvm::Value* GetNumArgs() const { ReleaseAssert(!m_isReturnContinuation); return m_valuePreserver.Get(x_numArgs); }
    llvm::Value* GetRetStart() const { ReleaseAssert(m_isReturnContinuation); return m_valuePreserver.Get(x_retStart); }
    llvm::Value* GetNumRet() const { ReleaseAssert(m_isReturnContinuation); return m_valuePreserver.Get(x_numRet); }

private:
    static constexpr const char* x_coroutineCtx = "coroutineCtx";
    static constexpr const char* x_stackBase = "stackBase";
    static constexpr const char* x_numArgs = "numArgs";
    static constexpr const char* x_retStart = "retStart";
    static constexpr const char* x_numRet = "numRet";

    LLVMValuePreserver m_valuePreserver;
    llvm::Module* m_module;
    llvm::Function* m_impl;
    llvm::Function* m_target;
    bool m_isReturnContinuation;
};

struct DeegenLibFuncInstanceInfo
{
    std::string m_implName;
    std::string m_wrapperName;
    bool m_isRc;
};

struct DeegenLibFuncProcessor
{
    static std::vector<DeegenLibFuncInstanceInfo> WARN_UNUSED ParseInfo(llvm::Module* module);
    static void DoLowering(llvm::Module* module);

    static constexpr const char* x_allDefsHolderSymbolName = "x_deegen_impl_all_lib_func_defs_in_this_tu";
};

}   // namespace dast

