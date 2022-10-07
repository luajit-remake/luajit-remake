#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

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
    GetCallTargetConsideringMetatableResult callTarget = GetCallTargetConsideringMetatable(callee);
    if (callTarget.m_target.m_value == 0)
    {
        ThrowError(MakeErrorMessageForUnableToCall(callee));
    }

    TValue* callBase = base + 3;
    if (unlikely(callTarget.m_invokedThroughMetatable))
    {
        callBase[0] = TValue::Create<tFunction>(callTarget.m_target.As());
        callBase[x_numSlotsForStackFrameHeader] = callee;
        callBase[x_numSlotsForStackFrameHeader + 1] = base[1];
        callBase[x_numSlotsForStackFrameHeader + 2] = base[2];
        MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 3 /*numArgs*/, ForLoopIterCallReturnContinuation);
    }
    else
    {
        callBase[0] = callee;
        callBase[x_numSlotsForStackFrameHeader] = base[1];
        callBase[x_numSlotsForStackFrameHeader + 1] = base[2];
        MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 2 /*numArgs*/, ForLoopIterCallReturnContinuation);
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
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
