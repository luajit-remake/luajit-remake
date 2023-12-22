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
    // The Lua standard specifies that the loop condition check is the following:
    //     '(step > 0 && start <= end) || (step <= 0 && start >= end)'
    //
    TValue step = base[2];

    // If 'step' is double NaN or non-double value, step.ViewAsDouble() will be NaN and this check won't pass.
    //
    if (likely(step.ViewAsDouble() > 0))
    {
        // This is tricky. Do not short-curcuit, as short-curcuit behavior would prevent fusing two NaN checks into one
        //
        bool t1 = !base[0].Is<tDoubleNotNaN>();
        bool t2 = !base[1].Is<tDoubleNotNaN>();
        bool tres = t1 | t2;    // intentionally bitwise or, not logical or!
        if (unlikely(tres))
        {
            EnterSlowPath<ForLoopInitSlowPath>();
        }

        double start = base[0].As<tDoubleNotNaN>();
        double end = base[1].As<tDoubleNotNaN>();
        if (start <= end)
        {
            base[3] = TValue::Create<tDouble>(start);
            Return();
        }
        else
        {
            ReturnAndBranch();
        }
    }
    else
    {
        // If 'step' is NaN or non-double value, branch to slow path
        //
        if (unlikely(!step.Is<tDoubleNotNaN>()))
        {
            EnterSlowPath<ForLoopInitSlowPath>();
        }

        // Having reached here, we know 'step' is a not-NaN double value, and 'step > 0' is false.
        // So 'step <= 0' is true
        //
        assert(step.As<tDouble>() <= 0);

        // See comment in the mirror branch
        //
        bool t1 = !base[0].Is<tDoubleNotNaN>();
        bool t2 = !base[1].Is<tDoubleNotNaN>();
        bool tres = t1 | t2;    // intentionally bitwise or, not logical or!
        if (unlikely(tres))
        {
            EnterSlowPath<ForLoopInitSlowPath>();
        }

        double start = base[0].As<tDoubleNotNaN>();
        double end = base[1].As<tDoubleNotNaN>();
        if (start >= end)
        {
            base[3] = TValue::Create<tDouble>(start);
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
    DeclareReads(Range(Op("base"), 3));
    DeclareWrites(Range(Op("base"), 4));
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
    CheckForInterpreterTierUp(true);
    Variant();
    DeclareReads(Range(Op("base"), 3));
    DeclareWrites(Range(Op("base"), 1), Range(Op("base") + 3, 1));
}

DEEGEN_END_BYTECODE_DEFINITIONS
