#pragma once

#include "deegen_jit_impl_creator_base.h"

namespace dast {

class BaselineJitImplCreator final : public JitImplCreatorBase
{
public:
    BaselineJitImplCreator(BytecodeIrComponent& bic)
        : JitImplCreatorBase(bic)
    { }

    BaselineJitImplCreator(SlowPathReturnContinuationTag, BytecodeIrComponent& bic)
        : JitImplCreatorBase(SlowPathReturnContinuationTag(), bic)
    { }

    virtual DeegenEngineTier WARN_UNUSED GetTier() const override { return DeegenEngineTier::BaselineJIT; }

    void DoLowering(BytecodeIrInfo* bii, const DeegenGlobalBytecodeTraitAccessor& gbta);
};

}   // namespace dast
