#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableDupImpl(TValue src)
{
    assert(src.Is<tTable>());
    VM* vm = VM::GetActiveVMForCurrentThread();
    TableObject* obj = TranslateToRawPointer(vm, src.As<tTable>());
    HeapPtr<TableObject> newObject = obj->ShallowCloneTableObject(vm);
    Return(TValue::Create<tTable>(newObject));
}

DEEGEN_DEFINE_BYTECODE(TableDup)
{
    Operands(
        Constant("src")
    );
    Result(BytecodeValue);
    Implementation(TableDupImpl);
    Variant(
        Op("src").IsConstant<tTable>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
