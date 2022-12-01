#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_bytecode_impl_creator_base.h"

namespace dast {

class BytecodeVariantDefinition;

class BaselineJitImplCreator final : public DeegenBytecodeImplCreatorBase
{
public:
    // This clones the input module, so that the input module is untouched
    //
    BaselineJitImplCreator(BytecodeIrComponent& bic);

    // The JIT tier needs different implementation for the fast-path return continuation (which should be JIT'ed code)
    // and the slow-path return continuation (which should be a C++ function but jumps back into JIT'ed code at the end)
    //
    // By default when a ReturnContinuation is passed in, we create the code for the fast path.
    // When this tag is specified, we create the code for the slow path.
    //
    struct SlowPathReturnContinuationTag { };
    BaselineJitImplCreator(SlowPathReturnContinuationTag, BytecodeIrComponent& bic);

    void DoLowering();

    virtual llvm::Module* GetModule() const override { return m_module.get(); }
    llvm::Value* GetOutputSlot() const { return m_valuePreserver.Get(x_outputSlot); }
    llvm::Value* GetCondBrDest() const { return m_valuePreserver.Get(x_condBrDest); }

    // The code pointer for the next bytecode
    //
    llvm::Value* GetFallthroughDest() const { return m_valuePreserver.Get(x_fallthroughDest); }

    std::string WARN_UNUSED GetResultFunctionName() { return m_resultFuncName; }

private:
    void CreateWrapperFunction();
    std::unique_ptr<llvm::Module> m_module;
    bool m_isSlowPathReturnContinuation;

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

    // The name of the wrapper function (i.e., the final product)
    //
    std::string m_resultFuncName;

    bool m_generated;

    static constexpr const char* x_outputSlot = "outputSlot";
    static constexpr const char* x_condBrDest = "condBrDest";
    static constexpr const char* x_fallthroughDest = "fallthroughDest";
};

}   // namespace dast
