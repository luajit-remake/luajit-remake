#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

// Handles the case where the loop start, end and step variable contains non-double value or double NaN value
//
static void NO_RETURN ForLoopInitSlowPath(TValue* base)
{
    double vals[3];
    for (uint32_t i = 0; i < 3; i++)
    {
        TValue v = base[i];
        if (likely(v.Is<tDouble>()))
        {
            vals[i] = v.As<tDouble>();
        }
        else if (v.Is<tString>())
        {
            HeapPtr<HeapString> hs = v.As<tString>();
            uint8_t* str = TranslateToRawPointer(hs->m_string);
            uint32_t len = hs->m_length;
            StrScanResult r = TryConvertStringToDoubleWithLuaSemantics(str, len);
            if (r.fmt == STRSCAN_ERROR)
            {
                ThrowError("'for' loop range must be a number");
            }
            assert(r.fmt == STRSCAN_NUM);
            vals[i] = r.d;
            base[i] = TValue::Create<tDouble>(r.d);
        }
        else
        {
            ThrowError("'for' loop range must be a number");
        }
    }

    // Note that the 'vals[2]' may be NaN, so '!(vals[2] > 0)' does not imply 'vals[2] <= 0'
    //
    bool loopConditionSatisfied = (vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1]);
    if (!loopConditionSatisfied)
    {
        ReturnAndBranch();
    }
    else
    {
        base[3] = TValue::Create<tDouble>(vals[0]);
        Return();
    }
}

static void NO_RETURN ForLoopInitImpl(TValue* base)
{
    if (unlikely(!base[0].Is<tDoubleNotNaN>() || !base[1].Is<tDoubleNotNaN>() || !base[2].Is<tDoubleNotNaN>()))
    {
        EnterSlowPath<ForLoopInitSlowPath>();
    }

    double vals[3];
    vals[0] = base[0].As<tDoubleNotNaN>();
    vals[1] = base[1].As<tDoubleNotNaN>();
    vals[2] = base[2].As<tDoubleNotNaN>();

    // Having reached here, we know 'vals[2]' is not NaN,
    // so the 'vals[2] <= 0' term in the loop condition check
    //     '(vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1])'
    // can be optimized out.
    //
    if (likely(vals[2] > 0))
    {
        if (vals[0] <= vals[1])
        {
            base[3] = TValue::Create<tDouble>(vals[0]);
            Return();
        }
        else
        {
            ReturnAndBranch();
        }
    }
    else
    {
        if (vals[0] >= vals[1])
        {
            base[3] = TValue::Create<tDouble>(vals[0]);
            Return();
        }
        else
        {
            ReturnAndBranch();
        }
    }
}

DEEGEN_DEFINE_BYTECODE(ForLoopInit)
{
    Operands(
        BytecodeRangeBaseRW("base")
    );
    Result(ConditionalBranch);
    Implementation(ForLoopInitImpl);
    Variant();
}

static void NO_RETURN ForLoopStepImpl(TValue* base)
{
    double vals[3];
    vals[0] = base[0].As<tDouble>();
    vals[1] = base[1].As<tDouble>();
    vals[2] = base[2].As<tDouble>();

    vals[0] += vals[2];

    // Note that if vals[2] is NaN, the for-loop would have exited without any iteration.
    // So having reached this bytecode, we know that vals[2] must not be NaN.
    // Therefore, the 'vals[2] <= 0' term in the loop condition check
    //     '(vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1])'
    // can be optimized out.
    //
    assert(!IsNaN(vals[2]));

    // Unfortunately we have to manually tail-duplicate the 'if', otherwise Clang would generate weird cmove code
    // (that indeed has one less branch, but should perform inferior in this specific case since the 'vals[2] > 0'
    // branch should be fairly predictable).
    //
    if (likely(vals[2] > 0))
    {
        if (likely(vals[0] <= vals[1]))
        {
            TValue v = TValue::Create<tDouble>(vals[0]);
            base[0] = v;
            base[3] = v;
            ReturnAndBranch();
        }
        else
        {
            Return();
        }
    }
    else
    {
        if (likely(vals[0] >= vals[1]))
        {
            TValue v = TValue::Create<tDouble>(vals[0]);
            base[0] = v;
            base[3] = v;
            ReturnAndBranch();
        }
        else
        {
            Return();
        }
    }
}

DEEGEN_DEFINE_BYTECODE(ForLoopStep)
{
    Operands(
        BytecodeRangeBaseRW("base")
    );
    Result(ConditionalBranch);
    Implementation(ForLoopStepImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
