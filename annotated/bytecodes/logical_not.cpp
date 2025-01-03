#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN LogicalNotImpl(TValue value)
{
    bool result = !value.IsTruthy();
    Return(TValue::Create<tBool>(result));
}

DEEGEN_DEFINE_BYTECODE(LogicalNot)
{
    Operands(
        BytecodeSlot("value")
    );
    Result(BytecodeValue);
    Implementation(LogicalNotImpl);
    Variant();
    DfgVariant();
    TypeDeductionRule([](TypeMask /*input*/) -> TypeMask { return x_typeMaskFor<tBool>; });
    RegAllocHint(
        Op("value").RegHint(RegHint::GPR),
        Op("output").RegHint(RegHint::GPR)
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
