#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TablePutByValMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN HandleInt64IndexNoMetamethodSlowPathPut(TValue base, TValue tvIndex, TValue valueToPut)
{
    double idxDbl = tvIndex.ViewAsDouble();
    int64_t index = static_cast<int64_t>(idxDbl);
    assert(UnsafeFloatEqual(idxDbl, static_cast<double>(index)));

    assert(base.Is<tTable>());
    TableObject* tableObj = TranslateToRawPointer(base.As<tTable>());
    VM* vm = VM::GetActiveVMForCurrentThread();
    tableObj->PutByIntegerIndexSlow(vm, index, valueToPut);
    Return();
}

// At this point we only know that 'tvIndex' is representable as a int64, and that the 'metamethod' shall be invoked to execute the PutByVal.
// We do not know if 'base' is table!
//
static void NO_RETURN HandleInt64IndexMetamethodSlowPath(TValue base, TValue tvIndex, TValue valueToPut, TValue metamethod)
{
    double idxDbl = tvIndex.ViewAsDouble();
    int64_t index = static_cast<int64_t>(idxDbl);
    assert(UnsafeFloatEqual(idxDbl, static_cast<double>(index)));

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
                MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, valueToPut, TablePutByValMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                TableObject* tableObj = TranslateToRawPointer(metamethod.As<tTable>());

                if (likely(!TCGet(tableObj->m_arrayType).MayHaveMetatable()))
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
                    base = TValue::Create<tTable>(TranslateToHeapPtr(tableObj));
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
            ThrowError("bad type for TablePutByVal");
        }
    }
}

static std::pair<TValue /*metamethod*/, bool /*hasMetamethod*/> ALWAYS_INLINE CheckShouldInvokeMetamethodForDoubleNotRepresentableAsInt64Index(HeapPtr<TableObject> tableObj, double indexDouble)
{
    ArrayType arrType = TCGet(tableObj->m_arrayType);
    if (likely(!arrType.MayHaveMetatable()))
    {
        return std::make_pair(TValue(), false);
    }

    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
    if (likely(gmr.m_result.m_value == 0))
    {
        return std::make_pair(TValue(), false);
    }

    HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
    if (likely(TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
    {
        return std::make_pair(TValue(), false);
    }

    if (arrType.HasSparseMap())
    {
        TValue originalVal = TableObject::QueryArraySparseMap(tableObj, indexDouble);
        if (likely(!originalVal.Is<tNil>()))
        {
            return std::make_pair(TValue(), false);
        }
    }

    TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
    if (metamethod.Is<tNil>())
    {
        return std::make_pair(TValue(), false);
    }

    return std::make_pair(metamethod, true);
}

// At this point we know that 'base' is table and 'index' is not representable as a int64
// Note that the 'base' might not equal the bytecode value here.
//
static void NO_RETURN HandleTableObjectNotInt64IndexSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue valueToPut, TValue base)
{
    assert(base.Is<tTable>());

    if (tvIndex.Is<tDouble>())
    {
        double indexDouble = tvIndex.As<tDouble>();
        assert(!UnsafeFloatEqual(indexDouble, static_cast<double>(static_cast<int64_t>(indexDouble))));

        if (unlikely(IsNaN(indexDouble)))
        {
            ThrowError("table index is NaN");
        }

        while (true)
        {
            assert(base.Is<tTable>());
            HeapPtr<TableObject> tableObj = base.As<tTable>();

            auto [metamethod, hasMetamethod] = CheckShouldInvokeMetamethodForDoubleNotRepresentableAsInt64Index(tableObj, indexDouble);
            if (likely(!hasMetamethod))
            {
                TableObject::RawPutByValDoubleIndex(tableObj, indexDouble, valueToPut);
                Return();
            }

double_index_handle_metamethod:
            assert(!metamethod.Is<tNil>());
            if (metamethod.Is<tFunction>())
            {
                MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, valueToPut, TablePutByValMetamethodCallContinuation);
            }

            // Recurse on 'metamethod[index]'
            //
            base = metamethod;
            if (base.Is<tTable>())
            {
                continue;
            }

            metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
            if (metamethod.Is<tNil>())
            {
                ThrowError("bad type for TablePutByVal");
            }
            goto double_index_handle_metamethod;
        }
    }
    else
    {
        UserHeapPointer<void> key;
        if (likely(tvIndex.Is<tHeapEntity>()))
        {
            key = tvIndex.As<tHeapEntity>();
        }
        else
        {
            assert(tvIndex.Is<tMIV>());
            if (unlikely(tvIndex.Is<tNil>()))
            {
                ThrowError("table index is nil");
            }
            assert(tvIndex.Is<tBool>());
            key = VM_GetSpecialKeyForBoolean(tvIndex.As<tBool>()).As<void>();
        }

        while (true)
        {
            assert(base.Is<tTable>());
            TableObject* tableObj = TranslateToRawPointer(base.As<tTable>());

            PutByIdICInfo icInfo;
            TableObject::PreparePutById(tableObj, key, icInfo /*out*/);

            TValue metamethod;
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
                            goto property_index_handle_metamethod;
                        }
                    }
                }
            }

            TableObject::PutById(tableObj, key, valueToPut, icInfo);
            Return();

property_index_handle_metamethod:
            assert(!metamethod.Is<tNil>());
            if (metamethod.Is<tFunction>())
            {
                MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, valueToPut, TablePutByValMetamethodCallContinuation);
            }

            // Recurse on 'metamethod[index]'
            //
            base = metamethod;
            if (base.Is<tTable>())
            {
                continue;
            }

            metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
            if (metamethod.Is<tNil>())
            {
                ThrowError("bad type for TablePutByVal");
            }
            goto property_index_handle_metamethod;
        }
    }
}

// At this point we know that 'base' is not table and 'tvIndex' is not representable as int64
//
static void NO_RETURN HandleNotTableObjectNotInt64IndexSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue valueToPut, TValue base)
{
    while (true)
    {
        assert(!base.Is<tTable>());
        TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TablePutByVal");
        }

        if (metamethod.Is<tFunction>())
        {
            MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, valueToPut, TablePutByValMetamethodCallContinuation);
        }

        base = metamethod;
        if (base.Is<tTable>())
        {
            EnterSlowPath<HandleTableObjectNotInt64IndexSlowPath>(base);
        }
    }
}

// At this point we know that 'tvIndex' is not representable as int64
//
static void NO_RETURN HandleNotInt64IndexSlowPath(TValue base, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    if (base.Is<tTable>())
    {
        EnterSlowPath<HandleTableObjectNotInt64IndexSlowPath>(base);
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectNotInt64IndexSlowPath>(base);
    }
}

// At this point all we know is that 'base' is not a table
//
static void NO_RETURN HandleNotTableObjectSlowPath(TValue base, TValue tvIndex, TValue /*valueToPut*/)
{
    assert(!base.Is<tTable>());

    double idxDbl = tvIndex.ViewAsDouble();
    int64_t index = static_cast<int64_t>(idxDbl);
    if (!UnsafeFloatEqual(idxDbl, static_cast<double>(index)))
    {
        // Not table object, not int64 index. We have a slow path for that.
        //
        EnterSlowPath<HandleNotTableObjectNotInt64IndexSlowPath>(base);
    }

    TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
    if (metamethod.Is<tNil>())
    {
        ThrowError("bad type for TablePutByVal");
    }
    // Index is int64 and we need to handle metamethod.
    //
    EnterSlowPath<HandleInt64IndexMetamethodSlowPath>(metamethod);
}

enum class TablePutByValIcResultKind
{
    NotTable,           // The base object is not a table
    HandleMetamethod,   // The base object is a table that has the __newindex metamethod, and the metamethod should be called
    NoMetamethod,       // The TablePutByVal has been executed fully
    SlowPathPut         // No metamethod needed, but fast path put failed. A slow path put is needed.
};

static void NO_RETURN TablePutByValImpl(TValue base, TValue tvIndex, TValue valueToPut)
{
    double idxDbl = tvIndex.ViewAsDouble();
    int64_t index = static_cast<int64_t>(idxDbl);
    // This is a hacky check that checks that whether 'tvIndex' is a double that represents an int64 value. It is correct because:
    // (1) If 'tvIndex' is a double, the correctness of the below check is clear.
    // (2) If 'tvIndex' is not a double, then 'idxDbl' will be NaN, so the below check is doomed to fail, also as desired.
    //
    if (likely(UnsafeFloatEqual(idxDbl, static_cast<double>(index))))
    {
        if (likely(base.Is<tHeapEntity>()))
        {
            TableObject* tableObj = TranslateToRawPointer(reinterpret_cast<HeapPtr<TableObject>>(base.As<tHeapEntity>()));
            ICHandler* ic = MakeInlineCache();
            ic->AddKey(tableObj->m_arrayType.m_asValue).SpecifyImpossibleValue(ArrayType::x_impossibleArrayType);
            ic->FuseICIntoInterpreterOpcode();

            using ResKind = TablePutByValIcResultKind;
            auto [metamethod, resKind] = ic->Body([ic, tableObj, index, valueToPut]() -> std::pair<TValue, ResKind> {
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
                                if (likely(tableObj->m_butterfly != nullptr && index == ArrayGrowthPolicy::x_arrayBaseOrd))
                                {
                                    Butterfly* butterfly = tableObj->m_butterfly;
                                    if (likely(butterfly->GetHeader()->m_arrayStorageCapacity > 0))
                                    {
                                        if (likely(TCGet(tableObj->m_hiddenClass).m_value == c_expectedHiddenClass.m_value))
                                        {
                                            *butterfly->UnsafeGetInVectorIndexAddr(ArrayGrowthPolicy::x_arrayBaseOrd) = valueToPut;
                                            butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;
                                            TCSet(tableObj->m_arrayType, c_newArrayType);
                                            TCSet(tableObj->m_hiddenClass, c_newHiddenClass);
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
                                if (likely(tableObj->m_butterfly != nullptr && index == ArrayGrowthPolicy::x_arrayBaseOrd))
                                {
                                    Butterfly* butterfly = tableObj->m_butterfly;
                                    if (likely(butterfly->GetHeader()->m_arrayStorageCapacity > 0))
                                    {
                                        if (likely(TCGet(tableObj->m_hiddenClass).m_value == c_expectedHiddenClass.m_value))
                                        {
                                            *butterfly->UnsafeGetInVectorIndexAddr(ArrayGrowthPolicy::x_arrayBaseOrd) = valueToPut;
                                            butterfly->GetHeader()->m_arrayLengthIfContinuous = 1;
                                            TCSet(tableObj->m_arrayType, c_newArrayType);
                                            TCSet(tableObj->m_hiddenClass, c_newHiddenClass);
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
                EnterSlowPath<HandleInt64IndexNoMetamethodSlowPathPut>();
            }
            case ResKind::HandleMetamethod:
            {
                EnterSlowPath<HandleInt64IndexMetamethodSlowPath>(metamethod);
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
    else
    {
        EnterSlowPath<HandleNotInt64IndexSlowPath>();
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
