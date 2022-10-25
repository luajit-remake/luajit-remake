#include "api_define_bytecode.h"
#include "deegen_api.h"

static void NO_RETURN BytecodeMovImpl(TValue input)
{
    Return(input);
}

DEEGEN_DEFINE_BYTECODE(Mov)
{
    Operands(
        BytecodeSlotOrConstant("input")
    );
    Result(BytecodeValue);
    Implementation(BytecodeMovImpl);
    Variant(
        Op("input").IsBytecodeSlot()
    );
    Variant(
        Op("input").IsConstant()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
