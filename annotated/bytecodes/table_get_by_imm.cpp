#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByImmMetamethodCallContinuation(TValue /*base*/, int16_t /*index*/)
{
    Return(GetReturnValue(0));
}

enum class TableGetByImmIcResultKind
{
    NotTable,           // The base object is not a table
    MayHaveMetatable,   // The base object is a table that may have metatable
    NoMetatable         // The base object is a table that is guaranteed to have no metatable
};

static void NO_RETURN TableGetByImmImpl(TValue base, int16_t index)
{
    enum {
        SlowPathNotTableObject,
        SlowPathCheckMetatable
    } slowpathKind;

    if (likely(base.Is<tHeapEntity>()))
    {
        HeapPtr<TableObject> heapEntity = reinterpret_cast<HeapPtr<TableObject>>(base.As<tHeapEntity>());
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(heapEntity->m_hiddenClass.m_value).SpecifyImpossibleValue(0);

        using ResKind = TableGetByImmIcResultKind;
        auto [result, resultKind] = ic->Body([ic, heapEntity, index]() -> std::pair<TValue, ResKind>
        {
            if (unlikely(heapEntity->m_type != HeapEntityType::Table))
            {
                return std::make_pair(TValue(), ResKind::NotTable);
            }

            GetByIntegerIndexICInfo c_icInfo;
            TableObject::PrepareGetByIntegerIndex(heapEntity, c_icInfo /*out*/);
            ResKind c_resKind = c_icInfo.m_mayHaveMetatable ? ResKind::MayHaveMetatable : ResKind::NoMetatable;

            if (unlikely(TCGet(heapEntity->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type != HeapEntityType::Structure))
            {
                // We cannot inline cache array access for CacheableDictionary or UncacheableDictionary
                //
                TValue res = TableObject::GetByIntegerIndex(heapEntity, index, c_icInfo);
                return std::make_pair(res, c_resKind);
            }

            if (likely(c_icInfo.m_isContinuous))
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

            switch (c_icInfo.m_icKind)
            {
            case GetByIntegerIndexICInfo::ICKind::VectorStorage:
            {
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
            case GetByIntegerIndexICInfo::ICKind::NoArrayPart:
            {
                return ic->Effect([c_resKind]() {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    return std::make_pair(TValue::Create<tNil>(), c_resKind);
                });
            }
            case GetByIntegerIndexICInfo::ICKind::VectorStorageXorSparseMap:
            {
                return ic->Effect([heapEntity, index, c_resKind]() {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    assert(TCGet(heapEntity->m_arrayType).HasSparseMap());
                    auto [res, success] = TableObject::TryAccessIndexInVectorStorage(heapEntity, index);
                    if (success)
                    {
                        return std::make_pair(res, c_resKind);
                    }
                    if (index < ArrayGrowthPolicy::x_arrayBaseOrd || static_cast<int64_t>(index) > ArrayGrowthPolicy::x_unconditionallySparseMapCutoff)
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
            case GetByIntegerIndexICInfo::ICKind::VectorStorageOrSparseMap:
            {
                return ic->Effect([heapEntity, index, c_resKind]() {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    assert(TCGet(heapEntity->m_arrayType).HasSparseMap());
                    auto [res, success] = TableObject::TryAccessIndexInVectorStorage(heapEntity, index);
                    if (success)
                    {
                        return std::make_pair(res, c_resKind);
                    }
                    return std::make_pair(TableObject::QueryArraySparseMap(heapEntity, static_cast<double>(index)), c_resKind);
                });
            }
            }   /* switch icKind */
        });

        if (likely(resultKind == ResKind::NoMetatable))
        {
            Return(result);
        }

        if (unlikely(resultKind == ResKind::NotTable))
        {
            slowpathKind = SlowPathNotTableObject;
            goto slowpath;
        }

        assert(resultKind == ResKind::MayHaveMetatable);
        if (likely(!result.Is<tNil>()))
        {
            Return(result);
        }
        slowpathKind = SlowPathCheckMetatable;
        goto slowpath;
    }
    else
    {
        slowpathKind = SlowPathNotTableObject;
        goto slowpath;
    }

slowpath:
    EnterSlowPath([base, index, slowpathKind]() mutable {
        TValue metamethod;

        switch (slowpathKind) {
        case SlowPathNotTableObject: goto not_table_object;
        case SlowPathCheckMetatable: goto check_metatable_for_table_object;
        }   /* switch slowpathKind */

check_metatable_for_table_object:
        {
            TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base.As<tTable>());
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
            Return(TValue::Create<tNil>());
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
                goto check_metatable_for_table_object;
            }
            Return(result);
        }
    });
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
