#include "api_define_bytecode.h"
#include "deegen_api.h"
#include "api_inline_cache.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByIdMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

// Forward declaration due to mutual recursion
//
static void NO_RETURN HandleMetatableSlowPath(TValue /*bc_base*/, TValue /*bc_tviIndex*/, TValue base, TValue metamethod);

// At this point, we know that 'rawget(base, index)' is nil and 'base' might have a metatable
//
static void NO_RETURN CheckMetatableSlowPath(TValue /*bc_base*/, TValue /*bc_index*/, TValue base)
{
    assert(base.Is<tTable>());
    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(TranslateToRawPointer(base.As<tTable>()));
    if (gmr.m_result.m_value != 0)
    {
        TableObject* metatable = TranslateToRawPointer(gmr.m_result.As<TableObject>());
        if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::Index)))
        {
            TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
            if (!metamethod.Is<tNil>())
            {
                EnterSlowPath<HandleMetatableSlowPath>(base, metamethod);
            }
        }
    }
    Return(TValue::Create<tNil>());
}

// At this point, we know that 'base' is not a table
//
static void NO_RETURN HandleNotTableObjectSlowPath(TValue /*bc_base*/, TValue /*bc_tvIndex*/, TValue base)
{
    assert(!base.Is<tTable>());
    TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::Index);
    if (metamethod.Is<tNil>())
    {
        ThrowError("bad type for TableGetById");
    }
    EnterSlowPath<HandleMetatableSlowPath>(base, metamethod);
}

// At this point, we know that 'rawget(base, index)' is nil, and 'base' has a non-nil metamethod which we shall use
//
static void NO_RETURN HandleMetatableSlowPath(TValue /*bc_base*/, TValue tvIndex, TValue base, TValue metamethod)
{
    // If 'metamethod' is a function, we should invoke the metamethod
    //
    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, TableGetByIdMetamethodCallContinuation);
    }

    // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
    //
    base = metamethod;

    if (unlikely(!base.Is<tTable>()))
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>(base);
    }

    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();

    TableObject* tableObj = TranslateToRawPointer(base.As<tTable>());
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);
    TValue result = TableObject::GetById(tableObj, index, icInfo);
    if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
    {
        EnterSlowPath<CheckMetatableSlowPath>(base);
    }
    Return(result);
}

enum class TableGetByIdIcResultKind
{
    NotTable,           // The base object is not a table
    MayHaveMetatable,   // The base object is a table that may have metatable
    NoMetatable         // The base object is a table that is guaranteed to have no metatable
};

static void NO_RETURN TableGetByIdImpl(TValue base, TValue tvIndex)
{
    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();

    // Note that we only check HeapEntity here (instead of tTable) since it's one less pointer dereference
    // As long as we know it's a heap entity, we can get its hidden class which the IC caches on.
    //
    if (likely(base.Is<tHeapEntity>()))
    {
        TableObject* heapEntity = reinterpret_cast<TableObject*>(TranslateToRawPointer(base.As<tHeapEntity>()));
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(heapEntity->m_hiddenClass.m_value).SpecifyImpossibleValue(0);
        ic->FuseICIntoInterpreterOpcode();

        using ResKind = TableGetByIdIcResultKind;
        auto [result, resultKind] = ic->Body([ic, heapEntity, index]() -> std::pair<TValue, ResKind>
        {
            // If the heapEntity isn't a table, there's nothing we can do here
            //
            if (unlikely(heapEntity->m_type != HeapEntityType::Table))
            {
                return std::make_pair(TValue(), ResKind::NotTable);
            }

            GetByIdICInfo c_info;
            TableObject::PrepareGetById(heapEntity, UserHeapPointer<HeapString> { index }, c_info /*out*/);
            ResKind c_resKind = c_info.m_mayHaveMetatable ? ResKind::MayHaveMetatable : ResKind::NoMetatable;
            switch (c_info.m_icKind)
            {
            case GetByIdICInfo::ICKind::UncachableDictionary:
            {
                assert(false && "unimplemented");
                __builtin_unreachable();
            }
            case GetByIdICInfo::ICKind::MustBeNil:
            {
                return ic->Effect([c_resKind] {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    return std::make_pair(TValue::Create<tNil>(), c_resKind);
                });
            }
            case GetByIdICInfo::ICKind::MustBeNilButUncacheable:
            {
                return std::make_pair(TValue::Create<tNil>(), c_resKind);
            }
            case GetByIdICInfo::ICKind::InlinedStorage:
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([heapEntity, c_slot, c_resKind] {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    IcSpecifyCaptureValueRange(c_slot, 0, 255);
                    TValue res = TCGet(heapEntity->m_inlineStorage[c_slot]);
                    return std::make_pair(res, c_resKind);
                });
            }
            case GetByIdICInfo::ICKind::OutlinedStorage:
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([heapEntity, c_slot, c_resKind] {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, Butterfly::x_namedPropOrdinalRangeMax);
                    TValue res = heapEntity->m_butterfly->GetNamedProperty(c_slot);
                    return std::make_pair(res, c_resKind);
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
            EnterSlowPath<HandleNotTableObjectSlowPath>(base);
        }
        case ResKind::MayHaveMetatable:
        {
            if (likely(!result.Is<tNil>()))
            {
                Return(result);
            }
            EnterSlowPath<CheckMetatableSlowPath>(base);
        }
        }   /* switch resultKind*/
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>(base);
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
