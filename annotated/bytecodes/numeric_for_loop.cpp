#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN ForLoopInitImpl(TValue* base)
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
    bool loopConditionSatisfied = (vals[2] > 0 && vals[0] <= vals[1]) || (vals[2] <= 0 && vals[0] >= vals[1]);

    if (loopConditionSatisfied)
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
