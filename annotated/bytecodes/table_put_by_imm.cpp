#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TablePutByImmMetamethodCallContinuation(TValue /*base*/, int16_t /*index*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN HandleMetamethodSlowPath(TValue base, int16_t index, TValue valueToPut, TValue metamethod)
{
    // If 'metamethod' is a function, we should invoke the metamethod.
    // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
    //
    while (true)
    {
        assert(!metamethod.Is<tNil>());
        if (likely(metamethod.Is<tHeapEntity>()))
        {
            HeapEntityType mmType = metamethod.GetHeapEntityType();
            if (mmType == HeapEntityType::Function)
            {
                MakeCall(metamethod.As<tFunction>(), base, TValue::Create<tDouble>(index), valueToPut, TablePutByImmMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                TableObject* tableObj = metamethod.As<tTable>();

                if (likely(!tableObj->m_arrayType.MayHaveMetatable()))
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

                    TableObject* metatable = gmr.m_result.As<TableObject>();
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
                    base = TValue::Create<tTable>(tableObj);
                    continue;
                }

no_metamethod:
                TableObject::RawPutByValIntegerIndex(tableObj, index, valueToPut);
                Return();
            }
        }

        // Now we know 'metamethod' is not a function or pointer, so we should locate its own exotic '__index' metamethod..
        // The difference is that if the metamethod is nil, we need to throw an error
        //
        base = metamethod;
        metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::NewIndex);
        if (metamethod.Is<tNil>())
        {
            // TODO: make error message consistent with Lua
            //
            ThrowError("bad type for TablePutByImm");
        }
    }
}

static void NO_RETURN HandleNotTableObjectSlowPath(TValue base, int16_t /*index*/, TValue /*valueToPut*/)
{
    assert(!base.Is<tTable>());
    TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
    if (metamethod.Is<tNil>())
    {
        ThrowError("bad type for TablePutByImm");
    }
    EnterSlowPath<HandleMetamethodSlowPath>(metamethod);
}

static void NO_RETURN HandleNoMetamethodSlowPathPut(TValue base, int16_t index, TValue valueToPut)
{
    assert(base.Is<tTable>());
    TableObject* tableObj = base.As<tTable>();
    VM* vm = VM::GetActiveVMForCurrentThread();
    tableObj->PutByIntegerIndexSlow(vm, index, valueToPut);
    Return();
}

enum class TablePutByImmIcResultKind
{
    NotTable,           // The base object is not a table
    HandleMetamethod,   // The base object is a table that has the __newindex metamethod, and the metamethod should be called
    NoMetamethod,       // The TablePutByImm has been executed fully
    SlowPathPut         // No metamethod needed, but fast path put failed. A slow path put is needed.
};

static void NO_RETURN TablePutByImmImpl(TValue base, int16_t index, TValue valueToPut)
{
    if (likely(base.Is<tHeapEntity>()))
    {
        TableObject* tableObj = reinterpret_cast<TableObject*>(base.As<tHeapEntity>());
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(tableObj->m_arrayType.m_asValue).SpecifyImpossibleValue(ArrayType::x_impossibleArrayType);
        ic->FuseICIntoInterpreterOpcode();

        using ResKind = TablePutByImmIcResultKind;
        auto [metamethod, resKind] = ic->Body([ic, tableObj, index, valueToPut]() -> std::pair<TValue, TablePutByImmIcResultKind> {
            if (unlikely(tableObj->m_type != HeapEntityType::Table))
            {
                return std::make_pair(TValue(), ResKind::NotTable);
            }

            PutByIntegerIndexICInfo c_info;
            TableObject::PreparePutByIntegerIndex(tableObj, index, valueToPut, c_info /*out*/);

            using IndexCheckKind = PutByIntegerIndexICInfo::IndexCheckKind;
            using ValueCheckKind = PutByIntegerIndexICInfo::ValueCheckKind;
            if (likely(!c_info.m_mayHaveMetatable))
            {
                switch (c_info.m_indexCheckKind)
                {
                case IndexCheckKind::Continuous:
                {
                    ValueCheckKind c_valueCK = c_info.m_valueCheckKind;
                    // DEVNOTE: when we support int32 type, we need to specialize for ValueCheckKind::Int32 as well. Same in the other places.
                    //
                    assert(c_valueCK == ValueCheckKind::Double || c_valueCK == ValueCheckKind::NotNil);
                    return ic->Effect([tableObj, index, valueToPut, c_valueCK]() {
                        IcSpecializeValueFullCoverage(c_valueCK, ValueCheckKind::Double, ValueCheckKind::NotNil);
                        if (likely(TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_valueCK)))
                        {
                            if (likely(TableObject::TryPutByIntegerIndexFastPath_ContinuousArray(tableObj, index, valueToPut)))
                            {
                                return std::make_pair(TValue(), ResKind::NoMetamethod);
                            }
                        }
                        return std::make_pair(TValue(), ResKind::SlowPathPut);
                    });
                }
                case IndexCheckKind::InBound:
                {
                    ValueCheckKind c_valueCK = c_info.m_valueCheckKind;
                    assert(c_valueCK == ValueCheckKind::DoubleOrNil || c_valueCK == ValueCheckKind::NoCheck);
                    return ic->Effect([tableObj, index, valueToPut, c_valueCK]() {
                        IcSpecializeValueFullCoverage(c_valueCK, ValueCheckKind::DoubleOrNil, ValueCheckKind::NoCheck);
                        if (likely(TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_valueCK)))
                        {
                            if (likely(TableObject::TryPutByIntegerIndexFastPath_InBoundPut(tableObj, index, valueToPut)))
                            {
                                return std::make_pair(TValue(), ResKind::NoMetamethod);
                            }
                        }
                        return std::make_pair(TValue(), ResKind::SlowPathPut);
                    });
                }
                case IndexCheckKind::NoArrayPart:
                {
                    ValueCheckKind c_valueCK = c_info.m_valueCheckKind;
                    assert(c_valueCK == ValueCheckKind::Double || c_valueCK == ValueCheckKind::NotNil);
                    SystemHeapPointer<void> c_expectedHiddenClass = c_info.m_hiddenClass;
                    SystemHeapPointer<void> c_newHiddenClass = c_info.m_newHiddenClass;
                    ArrayType c_newArrayType = c_info.m_newArrayType;
                    return ic->Effect([tableObj, index, valueToPut, c_valueCK, c_expectedHiddenClass, c_newHiddenClass, c_newArrayType]() {
                        IcSpecializeValueFullCoverage(c_valueCK, ValueCheckKind::Double, ValueCheckKind::NotNil);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_expectedHiddenClass);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_newHiddenClass);
                        if (likely(TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_valueCK)))
                        {
                            // PreparePutByIntegerIndex only create IC with IndexCheckKind::NoArrayPart if the index is x_arrayBaseOrd.
                            // And since this is PutByImm, the index is a constant, thus will never change in future runs of this IC.
                            //
                            assert(index == ArrayGrowthPolicy::x_arrayBaseOrd);
                            std::ignore = index;
                            if (likely(tableObj->m_butterfly != nullptr))
                            {
                                Butterfly* butterfly = tableObj->m_butterfly;
                                if (likely(butterfly->GetHeader()->m_arrayStorageCapacity > 0))
                                {
                                    if (likely(tableObj->m_hiddenClass.m_value == c_expectedHiddenClass.m_value))
                                    {
                                        *butterfly->UnsafeGetInVectorIndexAddr(ArrayGrowthPolicy::x_arrayBaseOrd) = valueToPut;
                                        butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;
                                        tableObj->m_arrayType = c_newArrayType;
                                        tableObj->m_hiddenClass = c_newHiddenClass;
                                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                                    }
                                }
                            }
                        }
                        if (valueToPut.Is<tNil>())
                        {
                            return std::make_pair(TValue(), ResKind::NoMetamethod);
                        }
                        return std::make_pair(TValue(), ResKind::SlowPathPut);
                    });
                }
                case IndexCheckKind::ForceSlowPath:
                {
                    return std::make_pair(TValue(), ResKind::SlowPathPut);
                }
                }   /* switch indexCheckKind */
            }
            else
            {
                switch (c_info.m_indexCheckKind)
                {
                case IndexCheckKind::Continuous:
                {
                    ValueCheckKind c_valueCK = c_info.m_valueCheckKind;
                    assert(c_valueCK == ValueCheckKind::Double || c_valueCK == ValueCheckKind::NotNil);
                    return ic->Effect([tableObj, index, valueToPut, c_valueCK]() {
                        IcSpecializeValueFullCoverage(c_valueCK, ValueCheckKind::Double, ValueCheckKind::NotNil);
                        // Check for metamethod call
                        //
                        {
                            // Since the array is continuous, the old value is nil iff it's out of range
                            //
                            bool isInRange = TableObject::CheckIndexFitsInContinuousArray(tableObj, index);
                            bool isOldValueNil = !isInRange;
                            if (unlikely(isOldValueNil))
                            {
                                TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                                if (unlikely(!mm.Is<tNil>()))
                                {
                                    return std::make_pair(mm, ResKind::HandleMetamethod);
                                }
                            }
                        }

                        if (likely(TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_valueCK)))
                        {
                            if (likely(TableObject::TryPutByIntegerIndexFastPath_ContinuousArray(tableObj, index, valueToPut)))
                            {
                                return std::make_pair(TValue(), ResKind::NoMetamethod);
                            }
                        }
                        return std::make_pair(TValue(), ResKind::SlowPathPut);
                    });
                }
                case IndexCheckKind::InBound:
                {
                    // The check for whether the old value is nil needs to go through a lot of code already, so don't IC for now
                    //
                    {
                        // Check for metamethod call
                        //
                        GetByIntegerIndexICInfo getIcInfo;
                        TableObject::PrepareGetByIntegerIndex(tableObj, getIcInfo /*out*/);
                        TValue originalVal = TableObject::GetByIntegerIndex(tableObj, index, getIcInfo);
                        if (unlikely(originalVal.Is<tNil>()))
                        {
                            TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                            if (unlikely(!mm.Is<tNil>()))
                            {
                                return std::make_pair(mm, ResKind::HandleMetamethod);
                            }
                        }
                    }

                    if (TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_info.m_valueCheckKind))
                    {
                        if (TableObject::TryPutByIntegerIndexFastPath_InBoundPut(tableObj, index, valueToPut))
                        {
                            return std::make_pair(TValue(), ResKind::NoMetamethod);
                        }
                    }

                    return std::make_pair(TValue(), ResKind::SlowPathPut);
                }
                case IndexCheckKind::NoArrayPart:
                {
                    ValueCheckKind c_valueCK = c_info.m_valueCheckKind;
                    assert(c_valueCK == ValueCheckKind::Double || c_valueCK == ValueCheckKind::NotNil);
                    SystemHeapPointer<void> c_expectedHiddenClass = c_info.m_hiddenClass;
                    SystemHeapPointer<void> c_newHiddenClass = c_info.m_newHiddenClass;
                    ArrayType c_newArrayType = c_info.m_newArrayType;
                    return ic->Effect([tableObj, index, valueToPut, c_valueCK, c_expectedHiddenClass, c_newHiddenClass, c_newArrayType]() {
                        IcSpecializeValueFullCoverage(c_valueCK, ValueCheckKind::Double, ValueCheckKind::NotNil);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_expectedHiddenClass);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_newHiddenClass);

                        {
                            // Check for metamethod call
                            // Since the table object has no array part, the old value must be nil
                            //
                            TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                            if (unlikely(!mm.Is<tNil>()))
                            {
                                return std::make_pair(mm, ResKind::HandleMetamethod);
                            }
                        }
                        if (likely(TableObject::CheckValueMeetsPreconditionForPutByIntegerIndexFastPath(valueToPut, c_valueCK)))
                        {
                            // PreparePutByIntegerIndex only create IC with IndexCheckKind::NoArrayPart if the index is x_arrayBaseOrd.
                            // And since this is PutByImm, the index is a constant, thus will never change in future runs of this IC.
                            //
                            assert(index == ArrayGrowthPolicy::x_arrayBaseOrd);
                            std::ignore = index;
                            if (likely(tableObj->m_butterfly != nullptr))
                            {
                                Butterfly* butterfly = tableObj->m_butterfly;
                                if (likely(butterfly->GetHeader()->m_arrayStorageCapacity > 0))
                                {
                                    if (likely(tableObj->m_hiddenClass.m_value == c_expectedHiddenClass.m_value))
                                    {
                                        *butterfly->UnsafeGetInVectorIndexAddr(ArrayGrowthPolicy::x_arrayBaseOrd) = valueToPut;
                                        butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;
                                        tableObj->m_arrayType = c_newArrayType;
                                        tableObj->m_hiddenClass = c_newHiddenClass;
                                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                                    }
                                }
                            }
                        }
                        if (valueToPut.Is<tNil>())
                        {
                            return std::make_pair(TValue(), ResKind::NoMetamethod);
                        }
                        return std::make_pair(TValue(), ResKind::SlowPathPut);
                    });
                }
                case IndexCheckKind::ForceSlowPath:
                {
                    {
                        // Check for metamethod call
                        //
                        GetByIntegerIndexICInfo getIcInfo;
                        TableObject::PrepareGetByIntegerIndex(tableObj, getIcInfo /*out*/);
                        TValue originalVal = TableObject::GetByIntegerIndex(tableObj, index, getIcInfo);
                        if (unlikely(originalVal.Is<tNil>()))
                        {
                            TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                            if (unlikely(!mm.Is<tNil>()))
                            {
                                return std::make_pair(mm, ResKind::HandleMetamethod);
                            }
                        }
                    }
                    return std::make_pair(TValue(), ResKind::SlowPathPut);
                }
                }   /* switch indexCheckKind */
            }
        });

        switch (resKind)
        {
        case ResKind::NoMetamethod: [[likely]]
        {
            Return();
        }
        case ResKind::SlowPathPut:
        {
            EnterSlowPath<HandleNoMetamethodSlowPathPut>();
        }
        case ResKind::HandleMetamethod:
        {
            EnterSlowPath<HandleMetamethodSlowPath>(metamethod);
        }
        case ResKind::NotTable: [[unlikely]]
        {
            EnterSlowPath<HandleNotTableObjectSlowPath>();
        }
        }   /* switch resKind */
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>();
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
