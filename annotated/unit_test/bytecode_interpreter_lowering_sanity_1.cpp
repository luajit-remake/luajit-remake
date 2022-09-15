#include "force_release_build.h"

#include "bytecode_definition_utils.h"
#include "deegen_api.h"

inline void NO_RETURN testfncont(TValue lhs, TValue /*rhs*/, TValue* /*retStart*/, size_t /*numRets*/)
{
    Return(lhs);
}

inline void NO_RETURN testfn(TValue lhs, TValue rhs)
{
    if (lhs.Is<tDouble>())
    {
        Return(rhs);
    }
    else if (lhs.Is<tFunction>())
    {
        MakeCall(lhs.As<tFunction>(), rhs, testfncont);
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
    Result(BytecodeValue);
    Implementation(testfn);
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsBytecodeSlot()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
