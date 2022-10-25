#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByIdMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN TableGetByIdImpl(TValue base, TValue tvIndex)
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
            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);
            TValue result = TableObject::GetById(tableObj, index, icInfo);

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
            ThrowError("bad type for TableGetById");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByIdMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TableGetById)
{
    Operands(
        BytecodeSlot("base"),
        Constant("index")
    );
    Result(BytecodeValue);
    Implementation(TableGetByIdImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
