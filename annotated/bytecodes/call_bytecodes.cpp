#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN CallOperationReturnContinuation(TValue* base, uint16_t /*numArgs*/, int16_t numRets)
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
static void NO_RETURN CheckMetatableSlowPath(TValue* /*base*/, uint16_t /*numArgs*/, int16_t /*numRets*/, TValue* argStart, uint16_t numArgs, TValue func)
{
    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(func);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(func));
    }

    if constexpr(passVariadicRes)
    {
        MakeCallPassingVariadicRes(callTarget, func, argStart, numArgs, CallOperationReturnContinuation);
    }
    else
    {
        MakeCall(callTarget, func, argStart, numArgs, CallOperationReturnContinuation);
    }
}

template<bool passVariadicRes>
static void NO_RETURN CallOperationImpl(TValue* base, uint16_t numArgs, int16_t /*numRets*/)
{
    TValue func = base[0];
    TValue* argStart = base + x_numSlotsForStackFrameHeader;

    if (likely(func.Is<tFunction>()))
    {
        if constexpr(passVariadicRes)
        {
            MakeInPlaceCallPassingVariadicRes(func.As<tFunction>(), argStart, numArgs, CallOperationReturnContinuation);
        }
        else
        {
            MakeInPlaceCall(func.As<tFunction>(), argStart, numArgs, CallOperationReturnContinuation);
        }
    }

    EnterSlowPath<CheckMetatableSlowPath<passVariadicRes>>(argStart, numArgs, func);
}

DEEGEN_DEFINE_BYTECODE_TEMPLATE(CallOperation, bool passVariadicRes)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numArgs"),
        Literal<int16_t>("numRets")
    );
    Result(NoOutput);
    Implementation(CallOperationImpl<passVariadicRes>);
    for (uint16_t numArgs = 0; numArgs < 6; numArgs++)
    {
        for (int16_t numRets = -1; numRets < 4; numRets++)
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
