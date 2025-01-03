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

static void NO_RETURN ForLoopIterCheckMetamethodSlowPath(TValue* /*base*/, uint16_t /*numRets*/, TValue* base, TValue callee)
{
    TValue* callBase = base + 3;
    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(callee);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(callee));
    }

    callBase[x_numSlotsForStackFrameHeader] = callee;
    callBase[x_numSlotsForStackFrameHeader + 1] = base[1];
    callBase[x_numSlotsForStackFrameHeader + 2] = base[2];
    MakeInPlaceCall(callTarget, callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 3 /*numArgs*/, ForLoopIterCallReturnContinuation);
}

static void NO_RETURN ForLoopIterImpl(TValue* base, uint16_t /*numRets*/)
{
    TValue callee = base[0];
    if (likely(callee.Is<tFunction>()))
    {
        MakeCall(callee.As<tFunction>(), base[1], base[2], ForLoopIterCallReturnContinuation);
    }
    else
    {
        EnterSlowPath<ForLoopIterCheckMetamethodSlowPath>(base, callee);
    }
}

DEEGEN_DEFINE_BYTECODE(ForLoopIter)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint16_t>("numRets")
    );
    Result(ConditionalBranch);
    Implementation(ForLoopIterImpl);
    CheckForInterpreterTierUp(true);
    Variant(Op("numRets").HasValue(1));
    Variant(Op("numRets").HasValue(2));
    Variant(Op("numRets").HasValue(3));
    Variant();
    DfgVariant(Op("numRets").HasValue(1));
    DfgVariant(Op("numRets").HasValue(2));
    DfgVariant(Op("numRets").HasValue(3));
    DfgVariant();
    DeclareReads(Range(Op("base"), 3));
    DeclareWrites(
        Range(Op("base") + 2, Op("numRets") + 1).TypeDeductionRule(ValueProfile)
    );
    DeclareUsedByInPlaceCall(Op("base") + 3);
}

DEEGEN_END_BYTECODE_DEFINITIONS
