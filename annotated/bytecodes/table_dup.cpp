#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableDupGeneralImpl(TValue src)
{
    Assert(src.Is<tTable>());
    VM* vm = VM::GetActiveVMForCurrentThread();
    TableObject* obj = TranslateToRawPointer(vm, src.As<tTable>());
    HeapPtr<TableObject> newObject = obj->ShallowCloneTableObject(vm);
    Return(TValue::Create<tTable>(newObject));
}

// TableDupGeneral gracefully handles all the edge cases, but is a bit slow.
// In practice we expect the edge cases are indeed edge cases, though.
// So the TableDup opcode below provides some faster implementations for the common cases.
//
DEEGEN_DEFINE_BYTECODE(TableDupGeneral)
{
    Operands(
        Constant("src")
    );
    Result(BytecodeValue);
    Implementation(TableDupGeneralImpl);
    Variant(
        Op("src").IsConstant<tTable>()
    );
    DfgVariant();
    TypeDeductionRule([](TypeMask /*src*/) -> TypeMask { return x_typeMaskFor<tTable>; });
    RegAllocHint(
        Op("src").RegHint(RegHint::GPR),
        Op("output").RegHint(RegHint::GPR)
    );
}

static void NO_RETURN TableDupSpecializedImpl(TValue src, uint8_t inlineCapacityStepping, uint8_t hasButterfly)
{
    Assert(src.Is<tTable>());
    VM* vm = VM::GetActiveVMForCurrentThread();
    TableObject* obj = TranslateToRawPointer(vm, src.As<tTable>());
    HeapPtr<TableObject> newObject = obj->ShallowCloneTableObjectForTableDup(vm, inlineCapacityStepping, static_cast<bool>(hasButterfly));
    Return(TValue::Create<tTable>(newObject));
}

DEEGEN_DEFINE_BYTECODE(TableDup)
{
    Operands(
        Constant("src"),
        Literal<uint8_t>("inlineCapacityStepping"),
        // This is really a bool, but our framework doesn't support bool type yet, which is unfortunate
        //
        Literal<uint8_t>("hasButterfly")
    );
    Result(BytecodeValue);
    Implementation(TableDupSpecializedImpl);
    for (uint8_t i = 0; i <= TableObject::TableDupMaxInlineCapacitySteppingForNoButterflyCase(); i++)
    {
        Variant(
            Op("src").IsConstant<tTable>(),
            Op("inlineCapacityStepping").HasValue(i),
            Op("hasButterfly").HasValue(0)
        );
        DfgVariant(
            Op("inlineCapacityStepping").HasValue(i),
            Op("hasButterfly").HasValue(0)
        );
    }
    for (uint8_t i = 0; i <= TableObject::TableDupMaxInlineCapacitySteppingForHasButterflyCase(); i++)
    {
        Variant(
            Op("src").IsConstant<tTable>(),
            Op("inlineCapacityStepping").HasValue(i),
            Op("hasButterfly").HasValue(1)
        );
        DfgVariant(
            Op("inlineCapacityStepping").HasValue(i),
            Op("hasButterfly").HasValue(1)
        );
    }
    TypeDeductionRule(
        [](TypeMask /*src*/, uint8_t /*inlineCapacityStepping*/, uint8_t /*hasButterfly*/) -> TypeMask
        {
            return x_typeMaskFor<tTable>;
        });
    RegAllocHint(
        Op("src").RegHint(RegHint::GPR),
        Op("output").RegHint(RegHint::GPR)
    );
    RegAllocMayBeDisabledDespiteRegHintGiven();
}

DEEGEN_END_BYTECODE_DEFINITIONS
