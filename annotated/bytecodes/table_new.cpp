#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableNewImpl(uint8_t inlineStorageSizeStepping, uint16_t arrayPartSizeHint)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    SystemHeapPointer<Structure> structure = Structure::GetInitialStructureForSteppingKnowingAlreadyBuilt(vm, inlineStorageSizeStepping);
    TableObject* obj;
    // We use the 'impl' version and pass in inlineCapacity directly so that Clang can do
    // constant propagation for specialized 'inlineStorageSizeStepping' values
    //
    [[clang::always_inline]] obj = TableObject::CreateEmptyTableObjectImpl(
        vm,
        TranslateToRawPointer(vm, structure.As()),
        internal::x_inlineStorageSizeForSteppingArray[inlineStorageSizeStepping] /*inlineCapacity*/,
        arrayPartSizeHint);
    Return(TValue::Create<tTable>(obj));
}

DEEGEN_DEFINE_BYTECODE(TableNew)
{
    Operands(
        Literal<uint8_t>("inlineStorageSizeStepping"),
        Literal<uint16_t>("arrayPartSizeHint")
    );
    Result(BytecodeValue);
    Implementation(TableNewImpl);
    for (uint8_t i = 0; i <= 2; i++)
    {
        Variant(
            Op("inlineStorageSizeStepping").HasValue(i),
            Op("arrayPartSizeHint").HasValue(0)
        );
    }
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
