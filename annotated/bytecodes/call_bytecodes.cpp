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
static void NO_RETURN CheckMetatableSlowPath(TValue* /*base*/, uint16_t numArgs, uint16_t /*numRets*/, TValue* argStart)
{
    TValue func = *(argStart - x_numSlotsForStackFrameHeader);

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(func);
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
        if constexpr(passVariadicRes)
        {
            MakeInPlaceCallPassingVariadicRes(func.As<tFunction>(), argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
        }
        else
        {
            MakeInPlaceCall(func.As<tFunction>(), argStart, numArgs, CallOperationReturnContinuation<storeVariadicRes>);
        }
    }

    // We don't really have to pass any argument, but passing 'argStart' breaks a load chain in the slow path (load 'base' -> load 'func'),
    // and even slightly improves fast path code (due to avoiding an LLVM deficiency in the hoisting heuristic..)
    // But passing more stuffs will pessimize fast path code due to increased reg pressure
    //
    EnterSlowPath<CheckMetatableSlowPath<passVariadicRes, storeVariadicRes>>(argStart);
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
                DfgVariant(
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
            DfgVariant(
                Op("numArgs").HasValue(numArgs),
                Op("numRets").HasValue(0)
            );
        }
    }
    if (!storeVariadicRes)
    {
        Variant();
        DfgVariant();
    }
    else
    {
        Variant(Op("numRets").HasValue(0));
        DfgVariant(Op("numRets").HasValue(0));
    }

    DeclareReads(
        Range(Op("base"), 1),
        Range(Op("base") + x_numSlotsForStackFrameHeader, Op("numArgs")),
        VariadicResults(passVariadicRes)
    );
    if (!storeVariadicRes)
    {
        DeclareWrites(
            Range(Op("base"), Op("numRets")).TypeDeductionRule(ValueProfile)
        );
        DeclareUsedByInPlaceCall(Op("base"));
    }
    else
    {
        DeclareWrites(VariadicResults());
        DeclareUsedByInPlaceCall(Op("base"));
    }
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Call, CallOperation, false /*passVariadicRes*/, false /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallM, CallOperation, true /*passVariadicRes*/, false /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallR, CallOperation, false /*passVariadicRes*/, true /*storeVariadicRes*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CallMR, CallOperation, true /*passVariadicRes*/, true /*storeVariadicRes*/);

DEEGEN_END_BYTECODE_DEFINITIONS
