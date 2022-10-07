#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByImmMetamethodCallContinuation(TValue /*base*/, int16_t /*index*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN TableGetByImmImpl(TValue base, int16_t index)
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
            TValue result;
            GetByIntegerIndexICInfo icInfo;
            TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
            result = TableObject::GetByIntegerIndex(tableObj, index, icInfo);

            if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
            {
                TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                if (gmr.m_result.m_value != 0)
                {
                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
                    {
                        metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
                        if (!metamethod.Is<tNil>())
                        {
                            goto handle_metamethod;
                        }
                    }
                }
            }

            Return(result);
        }

not_table_object:
        metamethod = GetMetamethodForValue(base, LuaMetamethodKind::Index);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TableGetByImm");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, TValue::Create<tInt32>(index), TableGetByImmMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TableGetByImm)
{
    Operands(
        BytecodeSlot("base"),
        Literal<int16_t>("index")
    );
    Result(BytecodeValue);
    Implementation(TableGetByImmImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
