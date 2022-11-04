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
    HeapPtr<HeapString> index = tvIndex.As<tString>();

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
                MakeCall(metamethod.As<tFunction>(), base, tvIndex, valueToPut, TablePutByIdMetamethodCallContinuation);
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

static void ALWAYS_INLINE StoreValueIntoTableObject(HeapPtr<TableObject> obj, PutByIdICInfo::ICKind kind, int32_t slot, TValue valueToPut)
{
    if (kind == PutByIdICInfo::ICKind::InlinedStorage)
    {
        TCSet(obj->m_inlineStorage[slot], valueToPut);
    }
    else
    {
        assert(kind == PutByIdICInfo::ICKind::OutlinedStorage);
        TCSet(*(obj->m_butterfly->GetNamedPropertyAddr(slot)), valueToPut);
    }
}

static TValue ALWAYS_INLINE GetOldValueFromTableObject(HeapPtr<TableObject> obj, PutByIdICInfo::ICKind kind, int32_t slot)
{
    if (kind == PutByIdICInfo::ICKind::InlinedStorage)
    {
        return TCGet(obj->m_inlineStorage[slot]);
    }
    else
    {
        assert(kind == PutByIdICInfo::ICKind::OutlinedStorage);
        return TCGet(*(obj->m_butterfly->GetNamedPropertyAddr(slot)));
    }
}

}   // namespace PutByIdICHelper

static void NO_RETURN TablePutByIdImpl(TValue base, TValue tvIndex, TValue valueToPut)
{
    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();

    if (likely(base.Is<tHeapEntity>()))
    {
        HeapPtr<TableObject> tableObj = reinterpret_cast<HeapPtr<TableObject>>(base.As<tHeapEntity>());
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

            if (unlikely(!c_info.m_isInlineCacheable))
            {
                // Currently since we don't have UncacheableDictionary, the only case for this is TransitionedToDictionaryMode
                //
                assert(c_info.m_icKind == PutByIdICInfo::ICKind::TransitionedToDictionaryMode);
                assert(!c_info.m_propertyExists);
                if (unlikely(TableObject::PutByIdNeedToCheckMetatable(tableObj, c_info)))
                {
                    TValue mm = GetNewIndexMetamethodFromTableObject(tableObj);
                    if (!mm.Is<tNil>())
                    {
                        return std::make_pair(mm, ResKind::HandleMetamethod);
                    }
                }
                VM* vm = VM::GetActiveVMForCurrentThread();
                TableObject* rawTab = TranslateToRawPointer(tableObj);
                assert(rawTab->m_hiddenClass.As<SystemHeapGcObjectHeader>()->m_type == HeapEntityType::Structure);
                rawTab->PutByIdTransitionToDictionary(vm, index, TranslateToRawPointer(vm, rawTab->m_hiddenClass.As<Structure>()), valueToPut);
                return std::make_pair(TValue(), ResKind::NoMetamethod);
            }

            assert(c_info.m_icKind == PutByIdICInfo::ICKind::InlinedStorage || c_info.m_icKind == PutByIdICInfo::ICKind::OutlinedStorage);
            if (likely(!c_info.m_mayHaveMetatable))
            {
                if (c_info.m_propertyExists)
                {
                    assert(!c_info.m_shouldGrowButterfly);
                    int32_t c_slot = c_info.m_slot;
                    PutByIdICInfo::ICKind c_icKind = c_info.m_icKind;
                    return ic->Effect([tableObj, valueToPut, c_icKind, c_slot] {
                        IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                        PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                    });
                }
                else
                {
                    if (c_info.m_shouldGrowButterfly)
                    {
                        ic->SetUncacheable();
                        TableObject* rawTab = TranslateToRawPointer(tableObj);
                        rawTab->GrowButterfly<true /*isGrowNamedStorage*/>(c_info.m_newStructure.As()->m_butterflyNamedStorageCapacity);
                    }

                    int32_t c_slot = c_info.m_slot;
                    PutByIdICInfo::ICKind c_icKind = c_info.m_icKind;
                    SystemHeapPointer<void> c_newStructure = c_info.m_newStructure.As();
                    return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                        IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
                        TCSet(tableObj->m_hiddenClass, c_newStructure);
                        PutByIdICHelper::StoreValueIntoTableObject(tableObj, c_icKind, c_slot, valueToPut);
                        return std::make_pair(TValue(), ResKind::NoMetamethod);
                    });
                }
            }
            else if (!c_info.m_propertyExists)
            {
                if (c_info.m_shouldGrowButterfly)
                {
                    ic->SetUncacheable();
                    TableObject* rawTab = TranslateToRawPointer(tableObj);
                    rawTab->GrowButterfly<true /*isGrowNamedStorage*/>(c_info.m_newStructure.As()->m_butterflyNamedStorageCapacity);
                }

                // The property is known to not exist, so we must always check the metatable
                //
                int32_t c_slot = c_info.m_slot;
                PutByIdICInfo::ICKind c_icKind = c_info.m_icKind;
                SystemHeapPointer<void> c_newStructure = c_info.m_newStructure.As();
                return ic->Effect([tableObj, valueToPut, c_icKind, c_slot, c_newStructure] {
                    IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
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
            else
            {
                assert(!c_info.m_shouldGrowButterfly);

                // The property exists, so we must check if its old value is nil and then check metatable
                //
                int32_t c_slot = c_info.m_slot;
                PutByIdICInfo::ICKind c_icKind = c_info.m_icKind;
                return ic->Effect([tableObj, valueToPut, c_icKind, c_slot] {
                    IcSpecializeValueFullCoverage(c_icKind, PutByIdICInfo::ICKind::InlinedStorage, PutByIdICInfo::ICKind::OutlinedStorage);
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
