#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

namespace {

void NO_RETURN CallOperationReturnContinuation(TValue* base, uint32_t /*numArgs*/, int32_t numRets)
{
    if (numRets < 0)
    {
        StoreReturnValuesAsVariadicResults();
    }
    else
    {
        StoreReturnValuesTo(base /*dst*/, static_cast<size_t>(numRets) /*numToStore*/);
    }
    Return();
}

template<bool passVariadicRes>
void NO_RETURN CallOperationImpl(TValue* base, uint32_t numArgs, int32_t /*numRets*/)
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
            MakeCallPassingVariadicRes(callTarget.m_target.As(), func, argStart, numArgs, CallOperationReturnContinuation);
        }
        else
        {
            MakeCall(callTarget.m_target.As(), func, argStart, numArgs, CallOperationReturnContinuation);
        }
    }
    else
    {
        if constexpr(passVariadicRes)
        {
            MakeInPlaceCallPassingVariadicRes(argStart, numArgs, CallOperationReturnContinuation);
        }
        else
        {
            MakeInPlaceCall(argStart, numArgs, CallOperationReturnContinuation);
        }
    }
}

}   // anonymous namespace

DEEGEN_DEFINE_BYTECODE_TEMPLATE(CallOperation, bool passVariadicRes)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint32_t>("numArgs"),
        Literal<int32_t>("numRets")
    );
    Result(NoOutput);
    Implementation(CallOperationImpl<passVariadicRes>);
    for (uint32_t numArgs = 0; numArgs < 6; numArgs++)
    {
        for (int32_t numRets = -1; numRets < 4; numRets++)
        {
            Variant(
                Op("numArgs").HasValue(numArgs),
                Op("numRets").HasValue(numRets)
            );
        }
    }
    Variant();
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Call, CallOperation, false /*passVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallM, CallOperation, true /*passVariadicRes*/);

DEEGEN_END_BYTECODE_DEFINITIONS
