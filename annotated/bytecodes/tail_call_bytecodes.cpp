#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

namespace {

template<bool passVariadicRes>
void NO_RETURN TailCallOperationImpl(TValue* base, uint32_t numArgs)
{
    TValue func = base[0];
    TValue* argStart = base + x_numSlotsForStackFrameHeader;
    GetCallTargetConsideringMetatableResult callTarget = GetCallTargetConsideringMetatable(func);
    if (callTarget.m_target.m_value == 0)
    {
        ThrowError(MakeErrorMessageForUnableToCall(func));
    }

    if (unlikely(callTarget.m_invokedThroughMetatable))
    {
        if constexpr(passVariadicRes)
        {
            MakeTailCallPassingVariadicRes(callTarget.m_target.As(), func, argStart, numArgs);
        }
        else
        {
            MakeTailCall(callTarget.m_target.As(), func, argStart, numArgs);
        }
    }
    else
    {
        if constexpr(passVariadicRes)
        {
            MakeInPlaceTailCallPassingVariadicRes(argStart, numArgs);
        }
        else
        {
            MakeInPlaceTailCall(argStart, numArgs);
        }
    }
}

}   // anonymous namespace

DEEGEN_DEFINE_BYTECODE_TEMPLATE(TailCallOperation, bool passVariadicRes)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint32_t>("numArgs")
    );
    Result(NoOutput);
    Implementation(TailCallOperationImpl<passVariadicRes>);
    for (uint32_t numArgs = 0; numArgs < 6; numArgs++)
    {
        Variant(Op("numArgs").HasValue(numArgs));
    }
    Variant();
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallT, TailCallOperation, false /*passVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallMT, TailCallOperation, true /*passVariadicRes*/);

DEEGEN_END_BYTECODE_DEFINITIONS
