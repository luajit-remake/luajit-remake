#include "bytecode_definition_utils.h"
#include "deegen_api.h"

inline void NO_RETURN testfn(TValue lhs, TValue rhs)
{
    if (lhs.Is<tDouble>() && rhs.Is<tDouble>())
    {
        Return(TValue::Create<tDouble>(lhs.As<tDouble>() + rhs.As<tDouble>()));
    }
    else if (lhs.Is<tMIV>() && rhs.Is<tMIV>())
    {
        bool a = lhs.As<tMIV>().IsTrue();
        bool b = rhs.As<tMIV>().IsTrue();
        Return(TValue::Create<tBool>(a && b));
    }
    else
    {
        Error("cannot handle");
    }
}

DEEGEN_DEFINE_BYTECODE(MyOpcode1)
{
    Operands(
        BytecodeSlotOrConstant("lhs"),
        BytecodeSlotOrConstant("rhs")
    );
    Implementation(testfn);
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tDouble>()
    );
    Variant(
        Op("lhs").IsConstant<tDouble>(),
        Op("rhs").IsBytecodeSlot()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tMIV>()
    );
    Variant(
        Op("lhs").IsConstant<tMIV>(),
        Op("rhs").IsBytecodeSlot()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsBytecodeSlot()
    );
}

inline void NO_RETURN testfn2(TValue x)
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

DEEGEN_DEFINE_BYTECODE(MyOpcode2)
{
    Operands(
        BytecodeSlotOrConstant("x")
    );
    Implementation(testfn2);
    Variant(
        Op("x").IsConstant<tMIV>()
    );
}

inline void NO_RETURN testfn3(TValue lhs, TValue rhs)
{
    if (!lhs.Is<tDouble>() || !rhs.Is<tDouble>())
    {
        Error("cannot handle");
    }
    else
    {
        Return(lhs);
    }
}


DEEGEN_END_BYTECODE_DEFINITIONS
