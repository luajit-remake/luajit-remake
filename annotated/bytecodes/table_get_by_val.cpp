#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByValMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

static std::pair<bool, TValue> WARN_UNUSED ALWAYS_INLINE GetIndexMetamethodFromTableObject(TableObject* tableObj)
{
    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
    if (gmr.m_result.m_value != 0)
    {
        TableObject* metatable = gmr.m_result.As<TableObject>();
        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
        {
            TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
            bool hasMetamethod = !metamethod.Is<tNil>();
            return std::make_pair(hasMetamethod, metamethod);
        }
    }
    return std::make_pair(false /*hasMetamethod*/, TValue());
}

// Forward declaration due to mutual recursion
//
static void NO_RETURN HandleNotTableObjectSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue base);

// At this point, we know that 'base' is table and 'rawget(base, index)' is nil, and we need to check the metatable of 'base'
//
static void NO_RETURN Int64IndexCheckMetatableSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue base)
{
    assert(base.Is<tTable>());
    TableObject* tableObj = base.As<tTable>();
    while (true)
    {
        // The invariant here is 'base' is table 'tableObj', 'base[index]' is nil, and we should check its metatable
        //
        auto [hasMetamethod, metamethod] = GetIndexMetamethodFromTableObject(tableObj);

        if (likely(!hasMetamethod))
        {
            Return(TValue::Create<tNil>());
        }

        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;

        if (unlikely(!base.Is<tTable>()))
        {
            EnterSlowPath<HandleNotTableObjectSlowPath>(base);
        }

        double idxDbl = tvIndex.ViewAsDouble();
        int64_t index = static_cast<int64_t>(idxDbl);
        assert(UnsafeFloatEqual(idxDbl, static_cast<double>(index)));

        tableObj = base.As<tTable>();
        GetByIntegerIndexICInfo icInfo;
        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
        TValue result = TableObject::GetByIntegerIndex(tableObj, index, icInfo);
        if (likely(!icInfo.m_mayHaveMetatable || !result.Is<tNil>()))
        {
            Return(result);
        }
    }
}

// At this point we know that 'index' is not a double representing an int64_t value
//
static void NO_RETURN HandleNotInt64IndexSlowPath(TValue /*bc_base*/, TValue /*bc_tvIndex*/, TValue base, double tvIndexViewAsDouble)
{
    if (likely(base.Is<tTable>()))
    {
        TValue tvIndex; tvIndex.m_value = cxx2a_bit_cast<uint64_t>(tvIndexViewAsDouble);
        if (likely(tvIndex.Is<tHeapEntity>()))
        {
            while (true)
            {
                assert(base.Is<tTable>());
                TableObject* tableObj = base.As<tTable>();
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, icInfo /*out*/);
                TValue result = TableObject::GetById(tableObj, UserHeapPointer<void> { tvIndex.As<tHeapEntity>() }, icInfo);
                if (likely(!icInfo.m_mayHaveMetatable || !result.Is<tNil>()))
                {
                    Return(result);
                }

                auto [hasMetamethod, metamethod] = GetIndexMetamethodFromTableObject(tableObj);

                if (likely(!hasMetamethod))
                {
                    Return(TValue::Create<tNil>());
                }

                // If 'metamethod' is a function, we should invoke the metamethod
                //
                if (likely(metamethod.Is<tFunction>()))
                {
                    MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
                }

                // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
                //
                base = metamethod;

                if (unlikely(!base.Is<tTable>()))
                {
                    EnterSlowPath<HandleNotTableObjectSlowPath>(base);
                }
            }
        }
        else if (likely(tvIndex.Is<tDouble>()))
        {
            double idx = tvIndex.As<tDouble>();
            assert(!TableObject::IsVectorQualifyingIndex(idx));
            while (true)
            {
                assert(base.Is<tTable>());
                TableObject* tableObj = base.As<tTable>();

                TValue result;
                if (unlikely(IsNaN(idx)))
                {
                    // Indexing a table by 'NaN' for read is not an error, but always results in nil,
                    // because indexing a table by 'NaN' for write is an error
                    //
                    result = TValue::Create<tNil>();
                }
                else
                {
                    // We already know that 'idx' is not a vector-qualifying index
                    // If it exists, it must be in the sparse map
                    //
                    GetByIntegerIndexICInfo icInfo;
                    TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
                    if (icInfo.m_icKind == GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap ||
                        icInfo.m_icKind == GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap)
                    {
                        assert(tableObj->m_arrayType.ArrayKind() != ArrayType::Kind::NoButterflyArrayPart);
                        result = TableObject::QueryArraySparseMap(tableObj, idx);
                    }
                    else
                    {
                        result = TValue::Create<tNil>();
                    }
                    if (likely(!icInfo.m_mayHaveMetatable))
                    {
                        Return(result);
                    }
                }
                if (likely(!result.Is<tNil>()))
                {
                    Return(result);
                }

                auto [hasMetamethod, metamethod] = GetIndexMetamethodFromTableObject(tableObj);

                if (likely(!hasMetamethod))
                {
                    Return(TValue::Create<tNil>());
                }

                // If 'metamethod' is a function, we should invoke the metamethod
                //
                if (likely(metamethod.Is<tFunction>()))
                {
                    MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
                }

                // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
                //
                base = metamethod;

                if (unlikely(!base.Is<tTable>()))
                {
                    EnterSlowPath<HandleNotTableObjectSlowPath>(base);
                }
            }
        }
        else
        {
            assert(tvIndex.Is<tMIV>());
            UserHeapPointer<HeapString> specialKey;
            if (tvIndex.Is<tNil>())
            {
                specialKey.m_value = 0;
            }
            else
            {
                assert(tvIndex.Is<tBool>());
                specialKey = VM_GetSpecialKeyForBoolean(tvIndex.As<tBool>());
                assert(specialKey.m_value != 0);
            }

            while (true)
            {
                assert(base.Is<tTable>());
                TableObject* tableObj = base.As<tTable>();

                TValue result;
                if (specialKey.m_value == 0)
                {
                    // Indexing a table by 'nil' for read is not an error, but always results in nil,
                    // because indexing a table by 'nil' for write is an error
                    //
                    result = TValue::Create<tNil>();
                }
                else
                {
                    GetByIdICInfo icInfo;
                    TableObject::PrepareGetById(tableObj, specialKey, icInfo /*out*/);
                    result = TableObject::GetById(tableObj, specialKey.As<void>(), icInfo);
                    if (likely(!icInfo.m_mayHaveMetatable))
                    {
                        Return(result);
                    }
                }
                if (likely(!result.Is<tNil>()))
                {
                    Return(result);
                }

                auto [hasMetamethod, metamethod] = GetIndexMetamethodFromTableObject(tableObj);

                if (likely(!hasMetamethod))
                {
                    Return(TValue::Create<tNil>());
                }

                // If 'metamethod' is a function, we should invoke the metamethod
                //
                if (likely(metamethod.Is<tFunction>()))
                {
                    MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
                }

                // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
                //
                base = metamethod;

                if (unlikely(!base.Is<tTable>()))
                {
                    EnterSlowPath<HandleNotTableObjectSlowPath>(base);
                }
            }
        }
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>(base);
    }
}

// This handles the case where 'base' is not a table.
// This is going to be an insanely slow path any way, so just let it decode 'tvIndex' from the bytecode struct
// (since in the main function fast path, the index is stored as a double in FPR, which requires an instruction to move it to GPR)
// However, note that 'base' must be passed in because it might be different from the original 'base'.
//
static void NO_RETURN HandleNotTableObjectSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue base)
{
    while (true)
    {
        assert(!base.Is<tTable>());
        TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::Index);
        if (metamethod.Is<tNil>())
        {
            ThrowError("bad type for TableGetByVal");
        }

        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, tvIndex, TableGetByValMetamethodCallContinuation);
        }

        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        base = metamethod;

        if (likely(base.Is<tTable>()))
        {
            break;
        }
    }

    double idxDbl = tvIndex.ViewAsDouble();
    int64_t index = static_cast<int64_t>(idxDbl);
    if (likely(UnsafeFloatEqual(idxDbl, static_cast<double>(index))))
    {
        // tvIndex is a double that represents a int64_t value
        //
        assert(base.Is<tTable>());
        TableObject* tableObj = base.As<tTable>();
        GetByIntegerIndexICInfo icInfo;
        TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
        TValue result = TableObject::GetByIntegerIndex(tableObj, index, icInfo);
        if (likely(!icInfo.m_mayHaveMetatable || !result.Is<tNil>()))
        {
            Return(result);
        }

        EnterSlowPath<Int64IndexCheckMetatableSlowPath>(base);
    }
    else
    {
        // Now, we know 'tvIndex' is not a int64 value, we can delegate to HandleNotInt64IndexSlowPath.
        // Note that we already have made forward progress (by checking metatable of 'base'), so the recursion here is fine.
        //
        EnterSlowPath<HandleNotInt64IndexSlowPath>(base, idxDbl);
    }
}

enum class TableGetByValIcResultKind
{
    NotTable,           // The base object is not a table
    MayHaveMetatable,   // The base object is a table that may have metatable
    NoMetatable         // The base object is a table that is guaranteed to have no metatable
};

static void NO_RETURN TableGetByValImpl(TValue base, TValue tvIndex)
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
            TableObject* heapEntity = reinterpret_cast<TableObject*>(base.As<tHeapEntity>());
            ICHandler* ic = MakeInlineCache();
            ic->AddKey(heapEntity->m_arrayType.m_asValue).SpecifyImpossibleValue(ArrayType::x_impossibleArrayType);
            ic->FuseICIntoInterpreterOpcode();

            using ResKind = TableGetByValIcResultKind;
            auto [result, resultKind] = ic->Body([ic, heapEntity, index]() -> std::pair<TValue, ResKind> {
                if (unlikely(heapEntity->m_type != HeapEntityType::Table))
                {
                    return std::make_pair(TValue(), ResKind::NotTable);
                }

                GetByIntegerIndexICInfo c_info;
                TableObject::PrepareGetByIntegerIndex(heapEntity, c_info /*out*/);
                ResKind c_resKind = c_info.m_mayHaveMetatable ? ResKind::MayHaveMetatable : ResKind::NoMetatable;

                if (likely(c_info.m_isContinuous))
                {
                    return ic->Effect([heapEntity, index, c_resKind]() {
                        IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                        bool isInRange = TableObject::CheckIndexFitsInContinuousArray(heapEntity, index);
                        if (isInRange)
                        {
                            TValue res = *(heapEntity->m_butterfly->UnsafeGetInVectorIndexAddr(index));
                            // We know that 'res' must not be nil thanks to the guarantee of continuous array, so no need to check metatable
                            //
                            assert(!res.Is<tNil>());
                            return std::make_pair(res, ResKind::NoMetatable);
                        }
                        else
                        {
                            return std::make_pair(TValue::Create<tNil>(), c_resKind);
                        }
                    });
                }

                switch (c_info.m_icKind)
                {
                case GetByIntegerIndexICInfo::ICKind::VectorStorage: {
                    return ic->Effect([heapEntity, index, c_resKind]() {
                        IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                        auto [res, success] = TableObject::TryAccessIndexInVectorStorage(heapEntity, index);
                        if (success)
                        {
                            return std::make_pair(res, c_resKind);
                        }
                        return std::make_pair(TValue::Create<tNil>(), c_resKind);
                    });
                }
                case GetByIntegerIndexICInfo::ICKind::NoArrayPart: {
                    return ic->Effect([c_resKind]() {
                        IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                        return std::make_pair(TValue::Create<tNil>(), c_resKind);
                    });
                }
                case GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap: {
                    return ic->Effect([heapEntity, index, c_resKind]() {
                        IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                        assert(heapEntity->m_arrayType.HasSparseMap());
                        auto [res, success] = TableObject::TryAccessIndexInVectorStorage(heapEntity, index);
                        if (success)
                        {
                            return std::make_pair(res, c_resKind);
                        }
                        if (index < ArrayGrowthPolicy::x_arrayBaseOrd || index > ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
                        {
                            return std::make_pair(TableObject::QueryArraySparseMap(heapEntity, static_cast<double>(index)), c_resKind);
                        }
                        else
                        {
                            // The sparse map is known to not contain vector-qualifying index, so the result must be nil
                            //
                            return std::make_pair(TValue::Create<tNil>(), c_resKind);
                        }
                    });
                }
                case GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap: {
                    return ic->Effect([heapEntity, index, c_resKind]() {
                        IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                        assert(heapEntity->m_arrayType.HasSparseMap());
                        auto [res, success] = TableObject::TryAccessIndexInVectorStorage(heapEntity, index);
                        if (success)
                        {
                            return std::make_pair(res, c_resKind);
                        }
                        return std::make_pair(TableObject::QueryArraySparseMap(heapEntity, static_cast<double>(index)), c_resKind);
                    });
                }
                } /* switch icKind */
            });

            switch (resultKind)
            {
            case ResKind::NoMetatable: [[likely]]
            {
                Return(result);
            }
            case ResKind::NotTable: [[unlikely]]
            {
                EnterSlowPath<HandleNotTableObjectSlowPath>(base);
            }
            case ResKind::MayHaveMetatable:
            {
                if (likely(!result.Is<tNil>()))
                {
                    Return(result);
                }
                EnterSlowPath<Int64IndexCheckMetatableSlowPath>(base);
            }
            }   /* switch resultKind*/
        }
        else
        {
            EnterSlowPath<HandleNotTableObjectSlowPath>(base);
        }
    }
    else
    {
        EnterSlowPath<HandleNotInt64IndexSlowPath>(base, idxDbl);
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
