#pragma once

#include "misc_llvm_helper.h"
#include "deegen_bytecode_impl_creator_base.h"

namespace dast {

class CPRuntimeConstantNodeBase;

class DeegenFunctionEntryLogicCreator
{
public:
    // numSpecializedFixedParams == -1 means it's not specialized
    //
    DeegenFunctionEntryLogicCreator(llvm::LLVMContext& ctx, DeegenEngineTier tier, bool acceptVarArgs, size_t numSpecializedFixedParams)
        : m_generated(false)
        , m_tier(tier)
        , m_acceptVarArgs(acceptVarArgs)
        , m_numSpecializedFixedParams(numSpecializedFixedParams)
    {
        Run(ctx);
    }

    std::string GetFunctionName();

    // Can only be called once
    //
    std::unique_ptr<llvm::Module> WARN_UNUSED GetInterpreterModule()
    {
        ReleaseAssert(m_tier == DeegenEngineTier::Interpreter);
        ReleaseAssert(m_module.get() != nullptr);
        return std::move(m_module);
    }

    struct BaselineJitCgResult
    {
        size_t m_fastPathLen;
        size_t m_slowPathLen;
        size_t m_dataSecLen;
        // Contains a function with name `GetFunctionName()` and prototype void(*)(void* fastPath, void* slowPath, void* dataSec) which emits the code
        //
        std::unique_ptr<llvm::Module> m_module;
        std::string m_patchFnName;
        std::string m_asmSourceForAudit;
    };

    // Can only be called once
    //
    BaselineJitCgResult WARN_UNUSED GetBaselineJitResult()
    {
        ReleaseAssert(m_tier == DeegenEngineTier::BaselineJIT);
        ReleaseAssert(m_module.get() != nullptr);
        return {
            .m_fastPathLen = m_baselineJitFastPathLen,
            .m_slowPathLen = m_baselineJitSlowPathLen,
            .m_dataSecLen = m_baselineJitDataSecLen,
            .m_module = std::move(m_module),
            .m_patchFnName = GetFunctionName(),
            .m_asmSourceForAudit = m_baselineJitSourceAsmForAudit
        };
    }

    bool IsNumFixedParamSpecialized() { return m_numSpecializedFixedParams != static_cast<size_t>(-1); }
    size_t GetSpecializedNumFixedParam() { ReleaseAssert(IsNumFixedParamSpecialized()); return m_numSpecializedFixedParams; }

    static std::unique_ptr<llvm::Module> WARN_UNUSED GenerateInterpreterTierUpOrOsrEntryImplementation(llvm::LLVMContext& ctx, bool isTierUp);

private:
    // Automatically invoked by constructor
    //
    void Run(llvm::LLVMContext& ctx);
    void GenerateBaselineJitStencil(std::unique_ptr<llvm::Module> srcModule);

    bool m_generated;
    DeegenEngineTier m_tier;
    bool m_acceptVarArgs;
    size_t m_numSpecializedFixedParams;
    std::unique_ptr<llvm::Module> m_module;

    // Only used by baseline JIT
    //
    size_t m_baselineJitFastPathLen;
    size_t m_baselineJitSlowPathLen;
    size_t m_baselineJitDataSecLen;
    std::string m_baselineJitSourceAsmForAudit;
};

}   // namespace dast
