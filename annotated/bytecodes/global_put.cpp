#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN GlobalPutMetamethodCallContinuation(TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN HandleMetamethodSlowPath(TValue tvIndex, TValue /*bc_valueToPut*/, TValue base, TValue valueToPut, TValue metamethod)
{
    // If 'metamethod' is a function, we should invoke the metamethod.
    // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
    //
    Assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();
    while (true)
    {
        Assert(!metamethod.Is<tNil>());
        if (likely(metamethod.Is<tHeapEntity>()))
        {
            HeapEntityType mmType = metamethod.GetHeapEntityType();
            if (mmType == HeapEntityType::Function)
            {
                MakeCall(metamethod.As<tFunction>(), base, tvIndex, valueToPut, GlobalPutMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                HeapPtr<TableObject> tableObj = metamethod.As<tTable>();
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);

                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
                {
                    metamethod = GetNewIndexMetamethodFromTableObject(tableObj);
                    if (!metamethod.Is<tNil>())
                    {
                        base = TValue::Create<tTable>(tableObj);
                        continue;
                    }
                }

                TableObject::PutById(tableObj, index, valueToPut, icInfo);
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
            ThrowError("bad type for GlobalPut");
        }
    }
}

static void NO_RETURN GlobalPutImpl(TValue tvIndex, TValue valueToPut)
{
    Assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();
    HeapPtr<TableObject> base = GetFEnvGlobalObject();

    ICHandler* ic = MakeInlineCache();
    ic->AddKey(base->m_hiddenClass.m_value).SpecifyImpossibleValue(0);
    ic->FuseICIntoInterpreterOpcode();
    auto [metamethod, hasMetamethod] = ic->Body([ic, base, index, valueToPut]() -> std::pair<TValue, bool> {
        PutByIdICInfo c_info;
        TableObject::PreparePutByIdForGlobalObject(base, UserHeapPointer<HeapString> { index }, c_info /*out*/);
        // We know that the global object must be a CacheableDictionary, so most fields in c_info should have determined values
        //
        Assert(c_info.m_isInlineCacheable && c_info.m_propertyExists && !c_info.m_shouldGrowButterfly);
        Assert(c_info.m_icKind == PutByIdICInfo::ICKind::InlinedStorage || c_info.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
        if (likely(!c_info.m_mayHaveMetatable))
        {
            if (c_info.m_icKind == PutByIdICInfo::ICKind::InlinedStorage)
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, valueToPut, c_slot] {
                    IcSpecifyCaptureValueRange(c_slot, 0, 255);
                    TCSet(base->m_inlineStorage[c_slot], valueToPut);
                    return std::make_pair(TValue(), false /*hasMetamethod*/);
                });
            }
            else
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, valueToPut, c_slot] {
                    IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, Butterfly::x_namedPropOrdinalRangeMax);
                    TCSet(*(base->m_butterfly->GetNamedPropertyAddr(c_slot)), valueToPut);
                    return std::make_pair(TValue(), false /*hasMetamethod*/);
                });
            }
        }
        else
        {
            if (c_info.m_icKind == PutByIdICInfo::ICKind::InlinedStorage)
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, valueToPut, c_slot] {
                    IcSpecifyCaptureValueRange(c_slot, 0, 255);
                    TValue oldValue = TCGet(base->m_inlineStorage[c_slot]);
                    if (unlikely(oldValue.Is<tNil>()))
                    {
                        TValue mm = GetNewIndexMetamethodFromTableObject(base);
                        if (unlikely(!mm.Is<tNil>()))
                        {
                            return std::make_pair(mm, true /*hasMetamethod*/);
                        }
                    }
                    TCSet(base->m_inlineStorage[c_slot], valueToPut);
                    return std::make_pair(TValue(), false /*hasMetamethod*/);
                });
            }
            else
            {
                int32_t c_slot = c_info.m_slot;
                return ic->Effect([base, valueToPut, c_slot] {
                    IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, Butterfly::x_namedPropOrdinalRangeMax);
                    TValue oldValue = TCGet(*(base->m_butterfly->GetNamedPropertyAddr(c_slot)));
                    if (unlikely(oldValue.Is<tNil>()))
                    {
                        TValue mm = GetNewIndexMetamethodFromTableObject(base);
                        if (unlikely(!mm.Is<tNil>()))
                        {
                            return std::make_pair(mm, true /*hasMetamethod*/);
                        }
                    }
                    TCSet(*(base->m_butterfly->GetNamedPropertyAddr(c_slot)), valueToPut);
                    return std::make_pair(TValue(), false /*hasMetamethod*/);
                });
            }
        }
    });

    if (likely(!hasMetamethod))
    {
        Return();
    }

    Assert(!metamethod.Is<tNil>());
    EnterSlowPath<HandleMetamethodSlowPath>(TValue::Create<tTable>(base), valueToPut, metamethod);
}

DEEGEN_DEFINE_BYTECODE(GlobalPut)
{
    Operands(
        Constant("index"),
        BytecodeSlot("value")
    );
    Result(NoOutput);
    Implementation(GlobalPutImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
    DfgVariant();
    RegAllocHint(
        Op("index").RegHint(RegHint::GPR)
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
