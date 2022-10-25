#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TablePutByValMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN TablePutByValImpl(TValue base, TValue tvIndex, TValue valueToPut)
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
            int64_t i64Index;
            if (tvIndex.Is<tInt32>())
            {
                i64Index = tvIndex.As<tInt32>();

handle_integer_index:
                PutByIntegerIndexICInfo icInfo;
                TableObject::PreparePutByIntegerIndex(tableObj, i64Index, valueToPut, icInfo /*out*/);

                if (likely(!icInfo.m_mayHaveMetatable))
                {
                    goto no_metamethod_integer_index;
                }
                else
                {
                    // Try to execute the fast checks to rule out __newindex metamethod first
                    //
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (likely(gmr.m_result.m_value == 0))
                    {
                        goto no_metamethod_integer_index;
                    }

                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (likely(TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                    {
                        goto no_metamethod_integer_index;
                    }

                    // Getting the metamethod from the metatable is more expensive than getting the index value,
                    // so get the index value and check if it's nil first
                    //
                    GetByIntegerIndexICInfo getIcInfo;
                    TableObject::PrepareGetByIntegerIndex(tableObj, getIcInfo /*out*/);
                    TValue originalVal = TableObject::GetByIntegerIndex(tableObj, i64Index, getIcInfo);
                    if (likely(!originalVal.Is<tNil>()))
                    {
                        goto no_metamethod_integer_index;
                    }

                    metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                    if (metamethod.Is<tNil>())
                    {
                        goto no_metamethod_integer_index;
                    }

                    // Now, we know we need to invoke the metamethod
                    //
                    goto handle_metamethod;
                }

no_metamethod_integer_index:
                if (!TableObject::TryPutByIntegerIndexFast(tableObj, i64Index, valueToPut, icInfo))
                {
                    VM* vm = VM::GetActiveVMForCurrentThread();
                    TableObject* obj = TranslateToRawPointer(vm, tableObj);
                    obj->PutByIntegerIndexSlow(vm, i64Index, valueToPut);
                }
            }
            else if (tvIndex.Is<tDouble>())
            {
                double indexDouble = tvIndex.As<tDouble>();
                if (likely(TableObject::IsInt64Index(indexDouble, i64Index /*out*/)))
                {
                    goto handle_integer_index;
                }

                if (unlikely(IsNaN(indexDouble)))
                {
                    ThrowError("table index is NaN");
                }

                ArrayType arrType = TCGet(tableObj->m_arrayType);
                if (likely(!arrType.MayHaveMetatable()))
                {
                    goto no_metamethod_double_index;
                }
                else
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (likely(gmr.m_result.m_value == 0))
                    {
                        goto no_metamethod_double_index;
                    }

                    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                    if (likely(TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                    {
                        goto no_metamethod_double_index;
                    }

                    if (arrType.HasSparseMap())
                    {
                        TValue originalVal = TableObject::QueryArraySparseMap(tableObj, indexDouble);
                        if (likely(!originalVal.Is<tNil>()))
                        {
                            goto no_metamethod_double_index;
                        }
                    }

                    metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                    if (metamethod.Is<tNil>())
                    {
                        goto no_metamethod_double_index;
                    }

                    goto handle_metamethod;
                }

no_metamethod_double_index:
                TableObject::RawPutByValDoubleIndex(tableObj, indexDouble, valueToPut);
            }
            else if (tvIndex.Is<tHeapEntity>())
            {
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, icInfo /*out*/);

                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (gmr.m_result.m_value != 0)
                    {
                        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                        {
                            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                            if (!metamethod.Is<tNil>())
                            {
                                goto handle_metamethod;
                            }
                        }
                    }
                }

                TableObject::PutById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, valueToPut, icInfo);
            }
            else
            {
                assert(tvIndex.Is<tMIV>());
                if (tvIndex.Is<tNil>())
                {
                    ThrowError("table index is nil");
                }
                assert(tvIndex.Is<tBool>());
                UserHeapPointer<HeapString> specialKey = VM_GetSpecialKeyForBoolean(tvIndex.As<tBool>());

                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tableObj, specialKey, icInfo /*out*/);

                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
                {
                    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
                    if (gmr.m_result.m_value != 0)
                    {
                        HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
                        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
                        {
                            metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                            if (!metamethod.Is<tNil>())
                            {
                                goto handle_metamethod;
                            }
                        }
                    }
                }

                TableObject::PutById(tableObj, specialKey.As<void>(), valueToPut, icInfo);
            }

            Return();
        }

not_table_object:
        metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TablePutByVal");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, valueToPut, TablePutByValMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;
    }
}

DEEGEN_DEFINE_BYTECODE(TablePutByVal)
{
    Operands(
        BytecodeSlot("base"),
        BytecodeSlot("index"),
        BytecodeSlot("value")
    );
    Result(NoOutput);
    Implementation(TablePutByValImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
