#include "api_define_bytecode.h"
#include "api_inline_cache.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN TablePutByIdMetamethodCallContinuation(TValue /*base*/, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN HandleMetamethodSlowPath(TValue base, TValue tvIndex, TValue valueToPut, TValue metamethod)
{
    assert(tvIndex.Is<tString>());
    HeapString* index = TranslateToRawPointer(tvIndex.As<tString>());

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
                MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, valueToPut, TablePutByIdMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                TableObject* tableObj = TranslateToRawPointer(metamethod.As<tTable>());
                PutByIdICInfo icInfo;
                TableObject::PreparePutById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);

                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, icInfo)))
                {
                    metamethod = GetNewIndexMetamethodFromTableObject(tableObj);
                    if (!metamethod.Is<tNil>())
                    {
                        base = TValue::Create<tTable>(TranslateToHeapPtr(tableObj));
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
            ThrowError("bad type for TablePutById");
        }
    }
}

static void NO_RETURN HandleNotTableObjectSlowPath(TValue base, TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    assert(!base.Is<tTable>());
    TValue metamethod = GetMetamethodForValue(base, LuaMetamethodKind::NewIndex);
    if (metamethod.Is<tNil>())
    {
        ThrowError("bad type for TablePutById");
    }
    EnterSlowPath<HandleMetamethodSlowPath>(metamethod);
}

enum class TablePutByIdIcResultKind
{
    NotTable,           // The base object is not a table
    HandleMetamethod,   // The base object is a table that has the __newindex metamethod
    NoMetamethod        // The TablePutById has been executed fully
};

namespace PutByIdICHelper
{

static void ALWAYS_INLINE StoreValueIntoTableObject(TableObject* obj, PutByIdICInfo::ICKind kind, int32_t slot, TValue valueToPut)
{
    if (kind == PutByIdICInfo::ICKind::InlinedStorage)
    {
        obj->m_inlineStorage[slot] = valueToPut;
    }
    else
    {
        assert(kind == PutByIdICInfo::ICKind::OutlinedStorage);
        *(obj->m_butterfly->GetNamedPropertyAddr(slot)) = valueToPut;
    }
}

static TValue ALWAYS_INLINE GetOldValueFromTableObject(TableObject* obj, PutByIdICInfo::ICKind kind, int32_t slot)
{
    if (kind == PutByIdICInfo::ICKind::InlinedStorage)
    {
        return obj->m_inlineStorage[slot];
    }
    else
    {
        assert(kind == PutByIdICInfo::ICKind::OutlinedStorage);
        return *(obj->m_butterfly->GetNamedPropertyAddr(slot));
    }
}

}   // namespace PutByIdICHelper

static void NO_RETURN TablePutByIdImpl(TValue base, TValue tvIndex, TValue valueToPut)
{
    assert(tvIndex.Is<tString>());
    HeapString* index = TranslateToRawPointer(tvIndex.As<tString>());

    if (likely(base.Is<tHeapEntity>()))
    {
        TableObject* tableObj = reinterpret_cast<TableObject*>(base.As<tHeapEntity>());
        ICHandler* ic = MakeInlineCache();
        ic->AddKey(tableObj->m_hiddenClass.m_value).SpecifyImpossibleValue(0);
        ic->FuseICIntoInterpreterOpcode();
        using ResKind = TablePutByIdIcResultKind;
        auto [metamethod, resKind] = ic->Body([ic, tableObj, index, valueToPut]() -> std::pair<TValue, ResKind> {
            if (unlikely(tableObj->m_type != HeapEntityType::Table))
            {
                return std::make_pair(TValue(), ResKind::NotTable);
            }

            PutByIdICInfo c_info;
            TableObject::PreparePutById(tableObj, UserHeapPointer<HeapString> { index } , c_info /*out*/);

            int32_t c_slot = c_info.m_slot;
            PutByIdICInfo::ICKind c_icKind = c_info.m_icKind;

            if (unlikely(!c_info.m_isInlineCacheable))
            {
                // Currently since we don't have UncacheableDictionary, the only case for this is TransitionedToDictionaryMode
                //
                assert(c_icKind == PutByIdICInfo::ICKind::TransitionedToDictionaryMode);
                assert(!c_info.m_propertyExists);
                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, c_info)))
                {
                    TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                    if (!mm.Is<tNil>())
                    {
                        return std::make_pair(mm, ResKind::HandleMetamethod);
                    }
                }
                TableObject::PutByIdTransitionToDictionary(tableObj, index, valueToPut);
                return std::make_pair(TValue(), ResKind::NoMetamethod);
            }

            assert(c_icKind == PutByIdICInfo::ICKind::InlinedStorage || c_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
            if (likely(!c_info.m_mayHaveMetatable))
            {
                if (c_info.m_propertyExists)
                {
                    assert(!c_info.m_shouldGrowButterfly);
                    return ic->Effect([tableObj, valueToPut, c_icKind, c_slot] {
                        IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                        IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);
                        PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                    });
                }
                else
                {
                    SystemHeapPointer<void> c_newStructure = c_info.m_newStructure.As();
                    assert(c_newStructure.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
                    if (unlikely(c_info.m_shouldGrowButterfly))
                    {
                        return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                            IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                            IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);
                            IcSpecifyCaptureAs2GBPointerNotNull(c_newStructure);
                            assert(TCGet(tableObj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
                            uint32_t oldButterflyNamedStorageCapacity = TCGet(tableObj->m_hiddenClass).As<Structure>()->m_butterflyNamedStorageCapacity;
                            TableObject::GrowButterflyNamedStorage_RT(tableObj, oldButterflyNamedStorageCapacity, c_newStructure.As<Structure>()->m_butterflyNamedStorageCapacity);
                            TCSet(tableObj->m_hiddenClass, c_newStructure);
                            PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                            return std::make_pair(TValue(), ResKind::NoMetamethod);
                        });
                    }
                    else
                    {
                        return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                            IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                            IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);
                            IcSpecifyCaptureAs2GBPointerNotNull(c_newStructure);
                            TCSet(tableObj->m_hiddenClass, c_newStructure);
                            PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                            return std::make_pair(TValue(), ResKind::NoMetamethod);
                        });
                    }
                }
            }
            else if (!c_info.m_propertyExists)
            {
                SystemHeapPointer<void> c_newStructure = c_info.m_newStructure.As();
                assert(c_newStructure.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);

                // The property is known to not exist, so we must always check the metatable
                //
                if (c_info.m_shouldGrowButterfly)
                {
                    return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                        IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                        IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_newStructure);

                        // It's critical for us to check the metatable first, and only call GrowButterfly()
                        // when we are certain the metamethod doesn't exist: once we call GrowButterfly(),
                        // we are committed to complete the rawput operation by updating the hidden class and writing the value.
                        //
                        // This is because the size of the butterfly is coded in the hidden class, so if we call GrowButterfly()
                        // but not update the hidden class, we are putting the table object in an inconsistent state.
                        //
                        TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                        if (unlikely(!mm.Is<tNil>()))
                        {
                            return std::make_pair(mm, ResKind::HandleMetamethod);
                        }

                        assert(TCGet(tableObj->m_hiddenClass).As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
                        uint32_t oldButterflyNamedStorageCapacity = TCGet(tableObj->m_hiddenClass).As<Structure>()->m_butterflyNamedStorageCapacity;
                        TableObject::GrowButterflyNamedStorage_RT(tableObj, oldButterflyNamedStorageCapacity, c_newStructure.As<Structure>()->m_butterflyNamedStorageCapacity);

                        TCSet(tableObj->m_hiddenClass, c_newStructure);
                        PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                    });
                }
                else
                {
                    return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                        IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                        IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);
                        IcSpecifyCaptureAs2GBPointerNotNull(c_newStructure);

                        TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                        if (unlikely(!mm.Is<tNil>()))
                        {
                            return std::make_pair(mm, ResKind::HandleMetamethod);
                        }
                        TCSet(tableObj->m_hiddenClass, c_newStructure);
                        PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                    });
                }
            }
            else
            {
                assert(!c_info.m_shouldGrowButterfly);

                // The property exists, so we must check if its old value is nil and then check metatable
                //
                return ic->Effect([tableObj, valueToPut, c_icKind, c_slot] {
                    IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                    IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, 255);

                    TValue oldValue = PutByIdICHelper::GetOldValueFromTableObject(tableObj, c_icKind, c_slot);
                    if (unlikely(oldValue.Is<tNil>()))
                    {
                        TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                        if (unlikely(!mm.Is<tNil>()))
                        {
                            return std::make_pair(mm, ResKind::HandleMetamethod);
                        }
                    }
                    PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                    return std::make_pair(TValue(), ResKind::NoMetamethod);
                });
            }
        });
        switch (resKind)
        {
        case ResKind::NoMetamethod: [[likely]]
        {
            Return();
        }
        case ResKind::NotTable: [[unlikely]]
        {
            EnterSlowPath<HandleNotTableObjectSlowPath>();
        }
        case ResKind::HandleMetamethod:
        {
            EnterSlowPath<HandleMetamethodSlowPath>(metamethod);
        }
        }   /*switch resKind*/
    }
    else
    {
        EnterSlowPath<HandleNotTableObjectSlowPath>();
    }
}

DEEGEN_DEFINE_BYTECODE(TablePutById)
{
    Operands(
        BytecodeSlot("base"),
        Constant("index"),
        BytecodeSlot("value")
    );
    Result(NoOutput);
    Implementation(TablePutByIdImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
