#include "bytecode_definition_utils.h"
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
    if (base[0].m_value == VM_GetVMTrueBaseLibNextFunctionObject().m_value)
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

static void NO_RETURN KVLoopIterImpl(TValue* base, uint8_t numRets)
{
    if (likely(base[0].m_value == VM_GetVMTrueBaseLibNextFunctionObject().m_value))
    {
        TableObjectIterator* iter = reinterpret_cast<TableObjectIterator*>(base + 2);
        HeapPtr<TableObject> table = base[1].As<tTable>();
        TableObjectIterator::KeyValuePair kv = iter->Advance(table);
        assert(1 <= numRets && numRets <= 2);
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
        // It turns out that this loop is actually not a table-kv-iteration loop
        // We have to execute the normal for-loop logic
        //
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
            MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 3 /*numArgs*/, KVLoopIterCallReturnContinuation);
        }
        else
        {
            callBase[0] = callee;
            callBase[x_numSlotsForStackFrameHeader] = base[1];
            callBase[x_numSlotsForStackFrameHeader + 1] = base[2];
            MakeInPlaceCall(callBase + x_numSlotsForStackFrameHeader /*argsBegin*/, 2 /*numArgs*/, KVLoopIterCallReturnContinuation);
        }
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
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
