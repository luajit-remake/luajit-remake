#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableVariadicPutBySeqImpl(TValue base, TValue tvIndex)
{
    assert(tvIndex.Is<tInt32>());
    int32_t indexStart = tvIndex.As<tInt32>();

    // 'base' is guaranteed to be a table object, as this opcode only shows up in a table initializer expression.
    //
    assert(base.Is<tTable>());
    HeapPtr<TableObject> tableObj = base.As<tTable>();

    // Since this opcode only shows up in a table initializer expression, 'base' must have no metatable.
    // For now we simply use a naive loop of PutByIntegerVal: this isn't performance sensitive after all, so just stay simple for now
    //
    TValue* src = VariadicResultsAccessor::GetPtr();
    int32_t numTermsToPut = static_cast<int32_t>(VariadicResultsAccessor::GetNum());
    for (int32_t i = 0; i < numTermsToPut; i++)
    {
        // We can safely ignore the case where we are putting so many items that it overflows int32_t. As Lua states:
        //     "Fields of the form 'exp' are equivalent to [i] = exp, where i are consecutive numerical
        //     integers, starting with 1. Fields in the other formats do not affect this counting."
        // So in order for this index to overflow, there needs to have 2^31 terms in the table, which is impossible
        //
        int32_t idx = indexStart + i;
        TableObject::RawPutByValIntegerIndex(tableObj, idx, src[i]);
    }
    Return();
}

DEEGEN_DEFINE_BYTECODE(TableVariadicPutBySeq)
{
    Operands(
        BytecodeSlot("base"),
        Constant("index")
    );
    Result(NoOutput);
    Implementation(TableVariadicPutBySeqImpl);
    Variant(
        Op("index").IsConstant<tInt32>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
