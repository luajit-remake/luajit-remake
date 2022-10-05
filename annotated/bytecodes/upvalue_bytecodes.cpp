#include "bytecode_definition_utils.h"
#include "deegen_api.h"

static void NO_RETURN UpvalueGetImpl(uint16_t ord)
{
    Return(UpvalueAccessor::Get(ord));
}

DEEGEN_DEFINE_BYTECODE(UpvalueGet)
{
    Operands(
        Literal<uint16_t>("ord")
    );
    Result(BytecodeValue);
    Implementation(UpvalueGetImpl);
    Variant();
}

static void NO_RETURN UpvalueSetImpl(uint16_t ord, TValue valueToPut)
{
    UpvalueAccessor::Put(ord, valueToPut);
    Return();
}

DEEGEN_DEFINE_BYTECODE(UpvaluePut)
{
    Operands(
        Literal<uint16_t>("ord"),
        BytecodeSlotOrConstant("value")
    );
    Result(NoOutput);
    Implementation(UpvalueSetImpl);
    Variant(Op("value").IsBytecodeSlot());
    Variant(Op("value").IsConstant());
}

static void NO_RETURN UpvalueCloseImpl(const TValue* base)
{
    UpvalueAccessor::Close(base);
    ReturnAndBranch();
}

DEEGEN_DEFINE_BYTECODE(UpvalueClose)
{
    Operands(
        BytecodeRangeBaseRO("base")
    );
    Result(ConditionalBranch);
    Implementation(UpvalueCloseImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
