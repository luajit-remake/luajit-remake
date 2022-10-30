#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN ForLoopIterCallReturnContinuation(TValue* base, uint16_t numRets)
{
    StoreReturnValuesTo(base + 3, numRets);
    TValue val = base[3];
    if (!val.Is<tNil>())
    {
        base[2] = val;
        ReturnAndBranch();
    }
    else
    {
        Return();
    }
}

static void NO_RETURN ForLoopIterImpl(TValue* base, uint16_t /*numRets*/)
{
    TValue callee = base[0];
    TValue* callBase = base + 3;
    if (likely(callee.Is<tFunction>()))
    {
        callBase[0] = callee;
        callBase[x_numSlotsForStackFrameHeader] = base[1];
        callBase[x_numSlotsForStackFrameHeader + 1] = base[2];
        MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 2 /*numArgs*/, ForLoopIterCallReturnContinuation);
    }

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(callee);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(callee));
    }

    callBase[0] = TValue::Create<tFunction>(callTarget);
    callBase[x_numSlotsForStackFrameHeader] = callee;
    callBase[x_numSlotsForStackFrameHeader + 1] = base[1];
    callBase[x_numSlotsForStackFrameHeader + 2] = base[2];
    MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 3 /*numArgs*/, ForLoopIterCallReturnContinuation);
}

DEEGEN_DEFINE_BYTECODE(ForLoopIter)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numRets")
    );
    Result(ConditionalBranch);
    Implementation(ForLoopIterImpl);
    Variant(Op("numRets").HasValue(1));
    Variant(Op("numRets").HasValue(2));
    Variant(Op("numRets").HasValue(3));
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
