#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

template<bool storeVariadicRes>
static void NO_RETURN CallOperationReturnContinuation(TValue* base, uint16_t /*numArgs*/, [[maybe_unused]] uint16_t numRets)
{
    if constexpr(!storeVariadicRes)
    {
        StoreReturnValuesTo(base /*dst*/, static_cast<size_t>(numRets) /*numToStore*/);
    }
    else
    {
        StoreReturnValuesAsVariadicResults();
    }
    Return();
}

template<bool passVariadicRes, bool storeVariadicRes>
static void NO_RETURN CheckMetatableSlowPath(TValue* /*base*/, uint16_t /*numArgs*/, uint16_t /*numRets*/, TValue* argStart, uint16_t numArgs, TValue func)
{
    FunctionObject* callTarget = GetCallTargetViaMetatable(func);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(func));
    }

    if constexpr(passVariadicRes)
    {
        MakeCallPassingVariadicRes(callTarget, func, argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
    }
    else
    {
        MakeCall(callTarget, func, argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
    }
}

template<bool passVariadicRes, bool storeVariadicRes>
static void NO_RETURN CallOperationImpl(TValue* base, uint16_t numArgs, uint16_t /*numRets*/)
{
    TValue func = base[0];
    TValue* argStart = base + x_numSlotsForStackFrameHeader;

    if (likely(func.Is<tFunction>()))
    {
        FunctionObject* callee = TranslateToRawPointer(func.As<tFunction>());
        if constexpr(passVariadicRes)
        {
            MakeInPlaceCallPassingVariadicRes(callee, argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
        }
        else
        {
            MakeInPlaceCall(callee, argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
        }
    }

    EnterSlowPath<CheckMetatableSlowPath<passVariadicRes, storeVariadicRes>>(argStart, numArgs, func);
}

DEEGEN_DEFINE_BYTECODE_TEMPLATE(CallOperation, bool passVariadicRes, bool storeVariadicRes)
{
    // If storeVariadicRes == true, "numRets" is not useful.
    // However, we still take this dummy parameter so we can easily reuse most of the code here
    // (we specify it to be 0 in all Variants, so it doesn't even have to sit in the bytecode struct).
    //
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numArgs"),
        Literal<uint16_t>("numRets")
    );
    Result(NoOutput);
    Implementation(CallOperationImpl<passVariadicRes, storeVariadicRes>);
    for (uint16_t numArgs = 0; numArgs < 6; numArgs++)
    {
        if (!storeVariadicRes)
        {
            for (uint16_t numRets = 0; numRets < 4; numRets++)
            {
                Variant(
                    Op("numArgs").HasValue(numArgs),
                    Op("numRets").HasValue(numRets)
                );
            }
        }
        else
        {
            Variant(
                Op("numArgs").HasValue(numArgs),
                Op("numRets").HasValue(0)
            );
        }
    }
    if (!storeVariadicRes)
    {
        Variant();
    }
    else
    {
        Variant(Op("numRets").HasValue(0));
    }

    DeclareReads(
        Range(Op("base"), 1),
        Range(Op("base") + x_numSlotsForStackFrameHeader, Op("numArgs")),
        VariadicResults(passVariadicRes)
    );
    if (!storeVariadicRes)
    {
        DeclareWrites(Range(Op("base"), Op("numRets")));
        DeclareClobbers(Range(Op("base") + Op("numRets"), Infinity()));
    }
    else
    {
        DeclareWrites(VariadicResults());
        DeclareClobbers(Range(Op("base"), Infinity()));
    }
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Call, CallOperation, false /*passVariadicRes*/, false /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallM, CallOperation, true /*passVariadicRes*/, false /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallR, CallOperation, false /*passVariadicRes*/, true /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallMR, CallOperation, true /*passVariadicRes*/, true /*storeVariadicRes*/);

DEEGEN_END_BYTECODE_DEFINITIONS
