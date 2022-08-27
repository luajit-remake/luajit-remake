#include "bytecode_definition_utils.h"

inline void testfn() {}

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
}

inline void testfn2() {}

DEEGEN_DEFINE_BYTECODE(MyOpcode2)
{
    Operands(
        BytecodeSlotOrConstant("x"),
        BytecodeSlotOrConstant("y")
    );
    Implementation(testfn2);
    Variant(
        Op("x").IsBytecodeSlot(),
        Op("y").IsConstant<tTable>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
