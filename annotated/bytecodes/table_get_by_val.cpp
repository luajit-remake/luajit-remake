#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByValMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN TableGetByValImpl(TValue base, TValue tvIndex)
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
            if (tvIndex.Is<tInt32>())
            {
                // TODO: we must be careful that we cannot do IC if the hidden class is CacheableDictionary
                //
                GetByIntegerIndexICInfo icInfo;
                TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                result = TableObject::GetByIntegerIndex(tableObj, tvIndex.As<tInt32>(), icInfo);
                if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
                {
                    goto check_metatable;
                }
            }
            else if (tvIndex.Is<tDouble>())
            {
                double indexDouble = tvIndex.As<tDouble>();
                if (unlikely(IsNaN(indexDouble)))
                {
                    // Indexing a table by 'NaN' for read is not an error, but always results in nil,
                    // because indexing a table by 'NaN' for write is an error
                    //
                    result = TValue::Create<tNil>();
                    goto check_metatable;
                }
                else
                {
                    GetByIntegerIndexICInfo icInfo;
                    TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                    result = TableObject::GetByDoubleVal(tableObj, indexDouble, icInfo);
                    if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
                    {
                        goto check_metatable;
                    }
                }
            }
            else if (tvIndex.Is<tHeapEntity>())
            {
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, icInfo /*out*/);
                result = TableObject::GetById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, icInfo);
                if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
                {
                    goto check_metatable;
                }
            }
            else
            {
                assert(tvIndex.Is<tMIV>());
                if (tvIndex.Is<tNil>())
                {
                    // Indexing a table by 'nil' for read is not an error, but always results in nil,
                    // because indexing a table by 'nil' for write is an error
                    //
                    result = TValue::Create<tNil>();
                    goto check_metatable;
                }
                else
                {
                    assert(tvIndex.Is<tBool>());
                    UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(tvIndex.As<tBool>());

                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(tableObj, specialKey, icInfo /*out*/);
                    result = TableObject::GetById(tableObj, specialKey.As<void>(), icInfo);
                    if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
                    {
                        goto check_metatable;
                    }
                }
            }

            Return(result);

check_metatable:
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

            Return(result);
        }

not_table_object:
        metamethod = GetMetamethodForValue(base, LuaMetamethodKind::Index);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TableGetByVal");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TableGetByVal)
{
    Operands(
        BytecodeSlot("base"),
        BytecodeSlot("index")
    );
    Result(BytecodeValue);
    Implementation(TableGetByValImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
