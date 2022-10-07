#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TablePutByImmMetamethodCallContinuation(TValue /*base*/, int16_t /*index*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN TablePutByImmImpl(TValue base, int16_t index, TValue valueToPut)
{
    TValue metamethod;

    while (true)
    {
        if (unlikely(!base.Is<tTable>()))
        {
            goto not_table_object;
        }

        {
            HeapPtr<TableObject> tableObj = base.As<tTable>();

            PutByIntegerIndexICInfo icInfo;
            TableObject::PreparePutByIntegerIndex(tableObj, index, valueToPut, icInfo /*out*/);

            if (likely(!icInfo.m_mayHaveMetatable))
            {
                goto no_metamethod;
            }
            else
            {
                // Try to execute the fast checks to rule out __newindex metamethod first
                //
                TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                if (likely(gmr.m_result.m_value == 0))
                {
                    goto no_metamethod;
                }

                HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                if (likely(TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                {
                    goto no_metamethod;
                }

                // Getting the metamethod from the metatable is more expensive than getting the index value,
                // so get the index value and check if it's nil first
                //
                GetByIntegerIndexICInfo getIcInfo;
                TableObject::PrepareGetByIntegerIndex(tableObj, getIcInfo /*out*/);
                TValue originalVal = TableObject::GetByIntegerIndex(tableObj, index, getIcInfo);
                if (likely(!originalVal.Is<tNil>()))
                {
                    goto no_metamethod;
                }

                metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                if (metamethod.Is<tNil>())
                {
                    goto no_metamethod;
                }

                // Now, we know we need to invoke the metamethod
                //
                goto handle_metamethod;
            }

no_metamethod:
            if (!TableObject::TryPutByIntegerIndexFast(tableObj, index, valueToPut, icInfo))
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                TableObject* obj = TranslateToRawPointer(vm, tableObj);
                obj->PutByIntegerIndexSlow(vm, index, valueToPut);
            }

            Return();
        }

not_table_object:
        metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TablePutByImm");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, TValue::Create<tInt32>(index), valueToPut, TablePutByImmMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TablePutByImm)
{
    Operands(
        BytecodeSlot("base"),
        Literal<int16_t>("index"),
        BytecodeSlot("value")
    );
    Result(NoOutput);
    Implementation(TablePutByImmImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
