#include "bytecode_definition_utils.h"
#include "deegen_api.h"

__attribute__((__used__)) extern "C" void NO_RETURN testfn1(TValue lhs, TValue rhs)
{
    if (lhs.Is<tDouble>() && rhs.Is<tDouble>())
    {
        Return(lhs);
    }
    else if (lhs.Is<tMIV>() && rhs.Is<tMIV>())
    {
        Return(rhs);
    }
    else
    {
        Error("cannot handle");
    }
}

__attribute__((__used__)) extern "C" void NO_RETURN testfn2(TValue x)
{
    if (x.Is<tBool>())
    {
        Return(x);
    }
    else if (x.Is<tNil>())
    {
        Return(TValue::Create<tDouble>(123));
    }
    else
    {
        Error("cannot handle");
    }
}

__attribute__((__used__)) extern "C" void NO_RETURN testfn3(TValue x)
{
    if (x.Is<tNil>())
    {
        Return(TValue::Create<tDouble>(234));
    }
    else if (x.Is<tBool>())
    {
        Return(x);
    }
    else
    {
        Error("cannot handle");
    }
}

__attribute__((__used__)) extern "C" void NO_RETURN testfn4(TValue x)
{
    if (x.Is<tTable>())
    {
        Return(TValue::Create<tDouble>(345));
    }
    else if (x.Is<tFunction>())
    {
        Return(x);
    }
    else if (x.Is<tDouble>())
    {
        Return(TValue::Create<tDouble>(456));
    }
    else
    {
        Error("cannot handle");
    }
}
