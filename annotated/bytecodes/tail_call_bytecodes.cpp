#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

template<bool passVariadicRes>
static void NO_RETURN TailCallCheckMetamethodSlowPath(TValue* /*base*/, uint32_t /*numArgs*/, TValue* base, uint32_t numArgs, TValue func)
{
    TValue* argStart = base + x_numSlotsForStackFrameHeader;
    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(func);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(func));
    }

    if constexpr(passVariadicRes)
    {
        MakeTailCallPassingVariadicRes(callTarget, func, argStart, numArgs);
    }
    else
    {
        MakeTailCall(callTarget, func, argStart, numArgs);
    }
}

template<bool passVariadicRes>
static void NO_RETURN TailCallOperationImpl(TValue* base, uint32_t numArgs)
{
    TValue func = base[0];
    TValue* argStart = base + x_numSlotsForStackFrameHeader;

    if (likely(func.Is<tFunction>()))
    {
        if constexpr(passVariadicRes)
        {
            MakeInPlaceTailCallPassingVariadicRes(func.As<tFunction>(), argStart, numArgs);
        }
        else
        {
            MakeInPlaceTailCall(func.As<tFunction>(), argStart, numArgs);
        }
    }
    else
    {
        EnterSlowPath<TailCallCheckMetamethodSlowPath<passVariadicRes>>(base, numArgs, func);
    }
}

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
