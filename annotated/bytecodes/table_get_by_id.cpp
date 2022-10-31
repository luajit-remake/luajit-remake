#include "api_define_bytecode.h"
#include "deegen_api.h"
#include "api_inline_cache.h"

#include "runtime_utils.h"

static void NO_RETURN TableGetByIdMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
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
    enum {
        SlowPathNotTableObject,
        SlowPathCheckMetatable
    } slowpathKind;

    // Note that we only check HeapEntity here (instead of tTable) since it's one less pointer dereference
    // As long as we know it's a heap entity, we can get its hidden class which the IC caches on.
    //
    if (likely(base.Is<tHeapEntity>()))
    {
        HeapPtr<UserHeapGcObjectHeader> heapEntity = base.As<tHeapEntity>();
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(heapEntity->m_hiddenClass).SpecifyImpossibleValue(0);

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
            TableObject::PrepareGetById(reinterpret_cast<HeapPtr<TableObject>>(heapEntity), UserHeapPointer<HeapString> { index }, c_info /*out*/);
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
                    TValue res = TCGet(reinterpret_cast<HeapPtr<TableObject>>(heapEntity)->m_inlineStorage[c_slot]);
                    return std::make_pair(res, c_resKind);
                });
            }
            case GetByIdICInfo::ICKind::OutlinedStorage:
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([heapEntity, c_slot, c_resKind] {
                    IcSpecializeValueFullCoverage(c_resKind, ResKind::MayHaveMetatable, ResKind::NoMetatable);
                    TValue res = reinterpret_cast<HeapPtr<TableObject>>(heapEntity)->m_butterfly->GetNamedProperty(c_slot);
                    return std::make_pair(res, c_resKind);
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
            ThrowError("bad type for TableGetById");
        }

handle_metamethod:
        // If 'metamethod' is a function, we should invoke the metamethod
        //
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), base, TValue::Create<tString>(index), TableGetByIdMetamethodCallContinuation);
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
            GetByIdICInfo icInfo;
            TableObject::PrepareGetById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);
            TValue result = TableObject::GetById(tableObj, index, icInfo);

            if (unlikely(icInfo.m_mayHaveMetatable && result.Is<tNil>()))
            {
                goto check_metatable_for_table_object;
            }
            Return(result);
        }
    });
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
