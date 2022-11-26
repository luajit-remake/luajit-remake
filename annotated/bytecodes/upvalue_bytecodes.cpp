#include "api_define_bytecode.h"
#include "deegen_api.h"

static void NO_RETURN MutableUpvalueGetImpl(uint16_t ord)
{
    Return(UpvalueAccessor::GetMutable(ord));
}

DEEGEN_DEFINE_BYTECODE(UpvalueGetMutable)
{
    Operands(
        Literal<uint16_t>("ord")
    );
    Result(BytecodeValue);
    Implementation(MutableUpvalueGetImpl);
    Variant();
}

static void NO_RETURN ImmutableUpvalueGetImpl(uint16_t ord)
{
    Return(UpvalueAccessor::GetImmutable(ord));
}

DEEGEN_DEFINE_BYTECODE(UpvalueGetImmutable)
{
    Operands(
        Literal<uint16_t>("ord")
    );
    Result(BytecodeValue);
    Implementation(ImmutableUpvalueGetImpl);
    Variant();
}

// Parser needs to late-replace between the two bytecodes
//
DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(UpvalueGetMutable, UpvalueGetImmutable);

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
