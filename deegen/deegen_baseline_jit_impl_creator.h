#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_ir_components.h"
#include "deegen_bytecode_impl_creator_base.h"
#include "deegen_stencil_runtime_constant_insertion_pass.h"
#include "deegen_stencil_creator.h"
#include "deegen_parse_asm_text.h"
#include "deegen_call_inline_cache.h"
#include "deegen_ast_inline_cache.h"

namespace dast {

class BytecodeVariantDefinition;
class DeegenGlobalBytecodeTraitAccessor;
struct BytecodeIrInfo;

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

    void DoLowering(BytecodeIrInfo* bii, const DeegenGlobalBytecodeTraitAccessor& gbta);

    virtual llvm::Module* GetModule() const override { return m_module.get(); }
    llvm::Value* GetOutputSlot() const { return m_valuePreserver.Get(x_outputSlot); }
    llvm::Value* GetCondBrDest() const { return m_valuePreserver.Get(x_condBrDest); }
    llvm::Value* GetJitSlowPathData() const { return m_valuePreserver.Get(x_jitSlowPathData); }

    // The code pointer for the next bytecode
    //
    llvm::Value* GetFallthroughDest() const { return m_valuePreserver.Get(x_fallthroughDest); }

    std::string WARN_UNUSED GetResultFunctionName() { return m_resultFuncName; }

    bool WARN_UNUSED IsBaselineJitSlowPathReturnContinuation() { return m_isSlowPathReturnContinuation; }

    bool WARN_UNUSED IsMainComponent() { return m_processKind == BytecodeIrComponentKind::Main; }

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

    X64AsmFile* WARN_UNUSED GetStencilPreTransformAsmFile()
    {
        return m_stencilPreTransformAsmFile.get();
    }

    std::string WARN_UNUSED GetStencilPostTransformAsmFile()
    {
        return m_stencilPostTransformAsmFile;
    }

    llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd);
    llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

    llvm::CallInst* WARN_UNUSED CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd);
    llvm::CallInst* WARN_UNUSED CreateOrGetConstantPlaceholderForOperand(size_t ordinal, llvm::Type* operandTy, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

    // Get the SlowPathData offset (uint64_t) for the current bytecode
    // Only available for use in JIT'ed code
    //
    llvm::Value* WARN_UNUSED GetSlowPathDataOffsetFromJitFastPath(llvm::BasicBlock* insertAtEnd);
    llvm::Value* WARN_UNUSED GetSlowPathDataOffsetFromJitFastPath(llvm::Instruction* insertBefore);

    std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::BasicBlock* insertAtEnd);
    std::pair<llvm::CallInst*, size_t /*ord*/> WARN_UNUSED CreateGenericIcStateCapturePlaceholder(llvm::Type* ty, int64_t lb, int64_t ub, llvm::Instruction* insertBefore);

    std::vector<DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult>& GetAllCallIcInfo()
    {
        return m_callIcInfo;
    }

    size_t GetNumTotalGenericIcCaptures()
    {
        return m_numGenericIcCaptures;
    }


    AstInlineCache::BaselineJitFinalLoweringResult& WARN_UNUSED GetGenericIcLoweringResult()
    {
        return m_genericIcLoweringResult;
    }

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

    // The rawly parsed stencil ASM file, before apply any ASM passes except the parser-applied ones (e.g., folding magic asm)
    // For audit and debugging purpose only
    //
    std::unique_ptr<X64AsmFile> m_stencilPreTransformAsmFile;

    // The ASM file that is used to be compiled to stencil object file (i.e., after ASM passes)
    // For audit and debugging purpose only
    //
    std::string m_stencilPostTransformAsmFile;

    std::string m_stencilObjectFile;

    DeegenStencil m_stencil;

    std::vector<DeegenCallIcLogicCreator::BaselineJitAsmLoweringResult> m_callIcInfo;

    size_t m_numGenericIcCaptures;

    AstInlineCache::BaselineJitFinalLoweringResult m_genericIcLoweringResult;

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

struct DeegenPlaceholderUtils
{
    static llvm::CallInst* WARN_UNUSED CreateConstantPlaceholderForOperand(llvm::Module* module,
                                                                           size_t ordinal,
                                                                           llvm::Type* operandTy,
                                                                           llvm::Instruction* insertBefore);

    // Return -1 if not found
    //
    static size_t WARN_UNUSED FindFallthroughPlaceholderOrd(const std::vector<CPRuntimeConstantNodeBase*>& rcDef);

    // Return "" if not found
    //
    static std::string WARN_UNUSED FindFallthroughPlaceholderSymbolName(const std::vector<CPRuntimeConstantNodeBase*>& rcDef);
};

}   // namespace dast
