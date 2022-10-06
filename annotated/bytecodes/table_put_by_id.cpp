#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN TablePutByIdMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN TablePutByIdImpl(TValue base, TValue tvIndex, TValue valueToPut)
{
    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();

    TValue metamethod;
    while (true)
    {
        if (unlikely(!base.Is<tTable>()))
        {
            goto not_table_object;
        }

        {
            HeapPtr<TableObject> tableObj = base.As<tTable>();
            PutByIdICInfo icInfo;
            TableObject::PreparePutById(tableObj, UserHeapPointer<HeapString> { index } , icInfo /*out*/);

            if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
            {
                TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                if (gmr.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                    {
                        metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                        if (!metamethod.IsNil())
                        {
                            goto handle_metamethod;
                        }
                    }
                }
            }

            TableObject::PutById(tableObj, index, valueToPut, icInfo);
            Return();
        }

not_table_object:
        metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TablePutById");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, valueToPut, TablePutByIdMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TablePutById)
{
    Operands(
        BytecodeSlot("base"),
        Constant("index"),
        BytecodeSlot("value")
    );
    Result(NoOutput);
    Implementation(TablePutByIdImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
