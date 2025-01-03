#pragma once

#include "deegen_jit_impl_creator_base.h"

namespace dast {

class BaselineJitImplCreator final : public JitImplCreatorBase
{
public:
    BaselineJitImplCreator(BytecodeIrInfo* bii, BytecodeIrComponent& bic)
        : JitImplCreatorBase(bii, bic)
    { }

    BaselineJitImplCreator(SlowPathReturnContinuationTag, BytecodeIrInfo* bii, BytecodeIrComponent& bic)
        : JitImplCreatorBase(SlowPathReturnContinuationTag(), bii, bic)
    { }

    virtual DeegenEngineTier WARN_UNUSED GetTier() const override { return DeegenEngineTier::BaselineJIT; }

    bool WARN_UNUSED HasCondBrTarget()
    {
        return GetBytecodeDef()->m_hasConditionalBranchTarget;
    }

    BaselineJitSlowPathDataLayout* WARN_UNUSED GetBaselineJitSlowPathDataLayout()
    {
        return GetBytecodeDef()->GetBaselineJitSlowPathDataLayout();
    }

    void DoLowering(BytecodeIrInfo* bii, const DeegenGlobalBytecodeTraitAccessor& gbta);

    static constexpr const char* x_slowPathSectionName = "deegen_baseline_jit_slow_path_section";
    static constexpr const char* x_codegenFnSectionName = "deegen_baseline_jit_codegen_fn_section";
    static constexpr const char* x_icCodegenFnSectionName = "deegen_baseline_jit_ic_codegen_fn_section";
};

}   // namespace dast
