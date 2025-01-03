#include "disable_assertions.h"

#include "deegen_api.h"

DEEGEN_DEFINE_LIB_FUNC(testfn1)
{
    Return();
}

DEEGEN_DEFINE_LIB_FUNC(testfn2)
{
    Return(TValue::Create<tBool>(true));
}

DEEGEN_DEFINE_LIB_FUNC(testfn3)
{
    Return(TValue::Create<tBool>(true), TValue::Create<tBool>(true), GetArg(0), GetArg(1));
}

DEEGEN_DEFINE_LIB_FUNC(testfn4)
{
    ReturnValueRange(GetStackBase(), GetNumArgs());
}

DEEGEN_DEFINE_LIB_FUNC(testfn5)
{
    ThrowError(GetArg(0));
}

DEEGEN_DEFINE_LIB_FUNC_CONTINUATION(testcont1)
{
    Return(TValue::Create<tDouble>(static_cast<int>(GetNumReturnValues())), *GetReturnValuesBegin());
}

DEEGEN_DEFINE_LIB_FUNC(testfn7)
{
    TValue* sb = GetStackBase();
    MakeInPlaceCall(sb + x_numSlotsForStackFrameHeader, 0, DEEGEN_LIB_FUNC_RETURN_CONTINUATION(testcont1));
}

DEEGEN_DEFINE_LIB_FUNC(testfn8)
{
    StackFrameHeader* hdr = GetStackFrameHeader();
    hdr = reinterpret_cast<StackFrameHeader*>(hdr->m_caller) - 1;
    TValue* sb = GetStackBase();
    sb[0] = TValue::Create<tBool>(true);
    LongJump(hdr, sb, 1);
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
