#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_bytecode_impl_creator_base.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "deegen_stencil_creator.h"

namespace dast {

class BytecodeVariantDefinition;

class BaselineJitImplCreator final : public DeegenBytecodeImplCreatorBase
{
public:
    // This clones the input module, so that the input module is untouched
    //
    BaselineJitImplCreator(BytecodeIrComponent& bic);

    virtual DeegenEngineTier WARN_UNUSED GetTier() const override { return DeegenEngineTier::BaselineJIT; }

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
    llvm::Value* GetJitSlowPathData() const { return m_valuePreserver.Get(x_jitSlowPathData); }

    // The code pointer for the next bytecode
    //
    llvm::Value* GetFallthroughDest() const { return m_valuePreserver.Get(x_fallthroughDest); }

    std::string WARN_UNUSED GetResultFunctionName() { return m_resultFuncName; }

    bool WARN_UNUSED IsBaselineJitSlowPathReturnContinuation() { return m_isSlowPathReturnContinuation; }

    // The baseline JIT slow path has access to the BaselineJitSlowPathData struct
    //
    bool WARN_UNUSED IsBaselineJitSlowPath()
    {
        return IsBaselineJitSlowPathReturnContinuation() || m_processKind == BytecodeIrComponentKind::SlowPath || m_processKind == BytecodeIrComponentKind::QuickeningSlowPath;
    }

    // Find the stencil runtime constant placeholder name corresponding to the fallthrough to the next bytecode, or "" if not found
    // Can only be used after 'm_stencilRcDefinitions' is populated
    //
    std::string WARN_UNUSED GetRcPlaceholderNameForFallthrough();

    std::vector<CPRuntimeConstantNodeBase*> WARN_UNUSED GetStencilRcDefinitions()
    {
        return m_stencilRcDefinitions;
    }

    std::string WARN_UNUSED GetStencilObjectFileContents()
    {
        return m_stencilObjectFile;
    }

    DeegenStencil WARN_UNUSED GetStencil()
    {
        return m_stencil;
    }

    llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd);
    llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

    llvm::CallInst* WARN_UNUSED CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd);
    llvm::CallInst* WARN_UNUSED CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

private:
    void CreateWrapperFunction();
    std::unique_ptr<llvm::Module> m_module;
    bool m_isSlowPathReturnContinuation;

    llvm::Function* m_impl;
    llvm::Function* m_wrapper;

    // The name of the wrapper function (i.e., the final product)
    //
    std::string m_resultFuncName;

    StencilRuntimeConstantInserter m_stencilRcInserter;
    std::vector<CPRuntimeConstantNodeBase*> m_stencilRcDefinitions;

    std::string m_stencilObjectFile;

    DeegenStencil m_stencil;

    bool m_generated;

    // This is analoguous to the bytecode pointer, except that it is only used by the baseline JIT slow path.
    //
    // Since the slow path is AOT-generated code, it needs to decode bytecode information similar to the interpreter.
    // However, the bytecode pointer won't work, because the slowpath needs additional information not available in the bytecode.
    // For example, it needs to jump back to the JIT'ed code at the end, but the address to branch to is not available in the interpreter bytecode.
    // As another example, the bytecode may need inline caching, and the baseline JIT IC logic is different from the interpreter IC logic.
    //
    // Therefore, the baseline JIT compiler will also generate a stream of BaselineJitSlowPathData structs.
    // Each bytecode in the bytecode stream corresponds uniquely to a BaselineJitSlowPathData struct.
    // The struct stores all the information needed by baseline JIT slowpath, such as the bytecode operands, the JIT address to branch to, the IC info, etc.
    //
    // At machine level, the baselineJitSlowPathData uses the register for curBytecode in interpreter, since in baseline JIT curBytecode is never needed.
    // It is only available for the slow path, because the JIT'ed code (fast path) will never need this struct.
    //
    static constexpr const char* x_jitSlowPathData = "jitSlowPathData";

    static constexpr const char* x_outputSlot = "outputSlot";

    static constexpr const char* x_condBrDest = "condBrDest";
    static constexpr const char* x_fallthroughDest = "fallthroughDest";
};

}   // namespace dast
