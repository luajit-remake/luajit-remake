#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN ValidateIsNextAndBranchImpl(TValue* base)
{
    bool validateOK = false;
    // A general for-loop can be specialized to a table-kv-iteration loop if the followings are true:
    //   1. base[0] is the true 'next' function
    //   2. base[1] is a table
    //   3. base[2] is nil
    //
    if (base[0].m_value == VM_GetLibFunctionObject<VM::LibFn::BaseNext>().m_value)
    {
        if (base[1].Is<tTable>())
        {
            if (base[2].Is<tNil>())
            {
                validateOK = true;
            }
        }
    }

    if (validateOK)
    {
        // Overwrite base[0] with a special object never exposed to the user
        //
        base[0] = VM_GetLibFunctionObject<VM::LibFn::BaseNextValidationOk>();

        // Overwrite base[2] with TableObjectIterator
        //
        static_assert(sizeof(TableObjectIterator) == 8);
        ConstructInPlace(reinterpret_cast<TableObjectIterator*>(base + 2));
    }

    ReturnAndBranch();
}

DEEGEN_DEFINE_BYTECODE(ValidateIsNextAndBranch)
{
    Operands(
        BytecodeRangeBaseRW("base")
    );
    Result(ConditionalBranch);
    Implementation(ValidateIsNextAndBranchImpl);
    Variant();
    DfgVariant();
    DeclareReads(Range(Op("base"), 3));
    DeclareWrites(
        Range(Op("base"), 1).TypeDeductionRule(DoNotProfile),
        Range(Op("base") + 2, 1).TypeDeductionRule(DoNotProfile)
    );
}

static void NO_RETURN KVLoopIterCallReturnContinuation(TValue* base, uint8_t numRets)
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

static void NO_RETURN KVLoopIterNotNextFunctionSlowPath(TValue* /*base*/, uint8_t /*numRets*/, TValue* base)
{
    // It turns out that this loop is actually not a table-kv-iteration loop
    // We have to execute the normal for-loop logic
    //
    TValue callee = base[0];
    TValue* callBase = base + 3;

    if (likely(callee.Is<tFunction>()))
    {
        callBase[x_numSlotsForStackFrameHeader] = base[1];
        callBase[x_numSlotsForStackFrameHeader + 1] = base[2];
        MakeInPlaceCall(callee.As<tFunction>(), callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 2 /*numArgs*/, KVLoopIterCallReturnContinuation);
    }

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(callee);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(callee));
    }

    callBase[x_numSlotsForStackFrameHeader] = callee;
    callBase[x_numSlotsForStackFrameHeader + 1] = base[1];
    callBase[x_numSlotsForStackFrameHeader + 2] = base[2];
    MakeInPlaceCall(callTarget, callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 3 /*numArgs*/, KVLoopIterCallReturnContinuation);
}

static void NO_RETURN KVLoopIterImpl(TValue* base, uint8_t numRets)
{
    if (likely(base[0].m_value == VM_GetLibFunctionObject<VM::LibFn::BaseNextValidationOk>().m_value))
    {
        TableObjectIterator* iter = reinterpret_cast<TableObjectIterator*>(base + 2);
        HeapPtr<TableObject> table = base[1].As<tTable>();
        TableObjectIterator::KeyValuePair kv = iter->Advance(table);
        Assert(1 <= numRets && numRets <= 2);
        base[3] = kv.m_key;
        if (numRets == 2)
        {
            base[4] = kv.m_value;
        }
        if (unlikely(kv.m_key.Is<tNil>()))
        {
            Return();
        }
        else
        {
            ReturnAndBranch();
        }
    }
    else
    {
        EnterSlowPath<KVLoopIterNotNextFunctionSlowPath>(base);
    }
}

DEEGEN_DEFINE_BYTECODE(KVLoopIter)
{
    Operands(
        BytecodeRangeBaseRW("base"),
        Literal<uint8_t>("numRets")
    );
    Result(ConditionalBranch);
    Implementation(KVLoopIterImpl);
    CheckForInterpreterTierUp(true);
    // LuaJIT frontend parser won't consider this bytecode unless numRets is 1 or 2, so no generalized variant needed
    //
    Variant(Op("numRets").HasValue(1));
    Variant(Op("numRets").HasValue(2));
    DfgVariant(Op("numRets").HasValue(1));
    DfgVariant(Op("numRets").HasValue(2));
    DeclareReads(Range(Op("base"), 3));
    DeclareWrites(
        Range(Op("base") + 2, 1).TypeDeductionRule(DoNotProfile),
        Range(Op("base") + 3, Op("numRets")).TypeDeductionRule(ValueProfile)
    );
    DeclareUsedByInPlaceCall(Op("base") + 3);
}

DEEGEN_END_BYTECODE_DEFINITIONS
