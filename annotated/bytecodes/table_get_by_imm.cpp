#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByImmMetamethodCallContinuation(TValue /*base*/, int16_t /*index*/)
{
    Return(GetReturnValue(0));
}

// Forward declaration due to mutual recursion
//
static void NO_RETURN HandleMetatableSlowPath(TValue /*bc_base*/, int16_t /*bc_index*/, TValue base, int16_t index, TValue metamethod);

// At this point, we know that 'rawget(base, index)' is nil and 'base' might have a metatable
//
static void NO_RETURN CheckMetatableSlowPath(TValue /*bc_base*/, int16_t /*bc_index*/, TValue base, int16_t index)
{
    assert(base.Is<tTable>());
    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base.As<tTable>());
    if (gmr.m_result.m_value != 0)
    {
        TableObject* metatable = gmr.m_result.As<TableObject>();
        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
        {
            TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
            if (!metamethod.Is<tNil>())
            {
                EnterSlowPath<HandleMetatableSlowPath>(base, index, metamethod);
            }
        }
    }
    Return(TValue::Create<tNil>());
}

// At this point, we know that 'base' is not a table
//
static void NO_RETURN HandleNotTableObjectSlowPath(TValue /*bc_base*/, int16_t /*bc_index*/, TValue base, int16_t index)
{
    assert(!base.Is<tTable>());
    TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::Index);
    if (metamethod.Is<tNil>())
    {
        ThrowError("bad type for TableGetByImm");
    }
    EnterSlowPath<HandleMetatableSlowPath>(base, index, metamethod);
}

// At this point, we know that 'rawget(base, index)' is nil, and 'base' has a non-nil metamethod which we shall use
//
static void NO_RETURN HandleMetatableSlowPath(TValue /*bc_base*/, int16_t /*bc_index*/, TValue base, int16_t index, TValue metamethod)
{
    assert(!metamethod.Is<tNil>());

    // If 'metamethod' is a function, we should invoke the metamethod
    //
    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(metamethod.As<tFunction>(), base, TValue::Create<tDouble>(index), TableGetByImmMetamethodCallContinuation);
    }

    // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
    //
    base = metamethod;

    if (unlikely(!base.Is<tTable>()))
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>(base, index);
    }

    TableObject* tableObj = base.As<tTable>();
    GetByIntegerIndexICInfo icInfo;
    TableObject::PrepareGetByIntegerIndex(tableObj, icInfo /*out*/);
    TValue result = TableObject::GetByIntegerIndex(tableObj, index, icInfo);
    if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
    {
        EnterSlowPath<CheckMetatableSlowPath>(base, index);
    }
    Return(result);
}

enum class TableGetByImmIcResultKind
{
    NotTable,           // The base object is not a table
    MayHaveMetatable,   // The base object is a table that may have metatable
    NoMetatable         // The base object is a table that is guaranteed to have no metatable
};

static void NO_RETURN TableGetByImmImpl(TValue base, int16_t index)
{
    if (likely(base.Is<tHeapEntity>()))
    {
        TableObject* heapEntity = reinterpret_cast<TableObject*>(base.As<tHeapEntity>());
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(heapEntity->m_arrayType.m_asValue).SpecifyImpossibleValue(ArrayType::x_impossibleArrayType);
        ic->FuseICIntoInterpreterOpcode();

        using ResKind = TableGetByImmIcResultKind;
        auto [result, resultKind] = ic->Body([ic, heapEntity, index]() -> std::pair<TValue, ResKind>
        {
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

        switch (resultKind)
        {
        case ResKind::NoMetatable: [[likely]]
        {
            Return(result);
        }
        case ResKind::NotTable: [[unlikely]]
        {
            EnterSlowPath<HandleNotTableObjectSlowPath>(base, index);
        }
        case ResKind::MayHaveMetatable:
        {
            if (likely(!result.Is<tNil>()))
            {
                Return(result);
            }
            EnterSlowPath<CheckMetatableSlowPath>(base, index);
        }
        }   /* switch resultKind*/
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>(base, index);
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
