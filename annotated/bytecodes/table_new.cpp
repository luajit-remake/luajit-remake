#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableNewImpl(uint8_t inlineStorageSizeStepping, uint16_t arrayPartSizeHint)
{
    VM* vm = VM::GetActiveVMForCurrentThread();
    SystemHeapPointer<Structure> structure = Structure::GetInitialStructureForSteppingKnowingAlreadyBuilt(vm, inlineStorageSizeStepping);
    HeapPtr<TableObject> obj = TableObject::CreateEmptyTableObject(vm, TranslateToRawPointer(vm, structure.As()), arrayPartSizeHint);
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
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
