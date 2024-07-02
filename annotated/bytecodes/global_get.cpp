#include "api_define_bytecode.h"
#include "deegen_api.h"
#include "api_inline_cache.h"

#include "runtime_utils.h"

static void NO_RETURN GlobalGetMetamethodCallContinuation(TValue /*tvIndex*/)
{
    Return(GetReturnValue(0));
}

// Forward declaration due to mutual recursion
//
static void NO_RETURN HandleMetatableSlowPath(TValue /*bc_tvIndex*/, TValue base, TValue metamethod);

// At this point, we know that 'rawget(base, index)' is nil and 'base' might have a metatable
//
static void NO_RETURN CheckMetatableSlowPath(TValue /*bc_tvIndex*/, TableObject* base)
{
    TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base);
    if (gmr.m_result.m_value != 0)
    {
        TableObject* metatable = TranslateToRawPointer(gmr.m_result.As<TableObject>());
        TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Index);
        if (!metamethod.Is<tNil>())
        {
            EnterSlowPath<HandleMetatableSlowPath>(TValue::Create<tTable>(base), metamethod);
        }
    }
    Return(TValue::Create<tNil>());
}

// At this point, we know that 'rawget(base, index)' is nil, and 'base' has a non-nil metamethod which we shall use
//
static void NO_RETURN HandleMetatableSlowPath(TValue tvIndex, TValue base, TValue metamethod)
{
    assert(base.Is<tTable>());
    while (true)
    {
        // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
        // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
        //
        if (likely(metamethod.Is<tHeapEntity>()))
        {
            HeapEntityType mmType = metamethod.GetHeapEntityType();
            if (mmType == HeapEntityType::Function)
            {
                MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), base, tvIndex, GlobalGetMetamethodCallContinuation);
            }
            else if (mmType == HeapEntityType::Table)
            {
                assert(tvIndex.Is<tString>());
                HeapString* index = TranslateToRawPointer(tvIndex.As<tString>());
                TableObject* tableObj = TranslateToRawPointer(metamethod.As<tTable>());
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(tableObj, UserHeapPointer<HeapString> { index }, icInfo /*out*/);
                TValue result = TableObject::GetById(tableObj, index, icInfo);
                if (likely(!icInfo.m_mayHaveMetatable || !result.Is<tNil>()))
                {
                    Return(result);
                }
                EnterSlowPath<CheckMetatableSlowPath>(tableObj);
            }
        }

        // Now we know 'metamethod' is not a function or table, so we should locate its own exotic '__index' metamethod..
        // The difference is that if the metamethod is nil, we need to throw an error
        //
        base = metamethod;
        metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::Index);
        if (metamethod.Is<tNil>())
        {
            // TODO: make error message consistent with Lua
            //
            ThrowError("bad type for GlobalGet");
        }
    }
}

static void NO_RETURN GlobalGetImpl(TValue tvIndex)
{
    assert(tvIndex.Is<tString>());
    HeapString* index = TranslateToRawPointer(tvIndex.As<tString>());
    TableObject* base = GetFEnvGlobalObject();

    ICHandler* ic = MakeInlineCache();
    ic->AddKey(base->m_hiddenClass.m_value).SpecifyImpossibleValue(0);
    ic->FuseICIntoInterpreterOpcode();
    auto [result, mayHaveMt] = ic->Body([ic, base, index]() -> std::pair<TValue, bool> {
        GetByIdICInfo c_info;
        // We know that the global object is always a CacheableDictionary
        //
        TableObject::PrepareGetByIdForGlobalObject(base, UserHeapPointer<HeapString> { index }, c_info /*out*/);
        bool c_mayHaveMt = c_info.m_mayHaveMetatable;
        if (c_info.m_icKind == GetByIdICInfo::ICKind::InlinedStorage)
        {
            int32_t c_slot = c_info.m_slot;
            return ic->Effect([base, c_slot, c_mayHaveMt] {
                IcSpecializeValueFullCoverage(c_mayHaveMt, false, true);
                IcSpecifyCaptureValueRange(c_slot, 0, 255);
                return std::make_pair(TCGet(base->m_inlineStorage[c_slot]), c_mayHaveMt);
            });
        }
        else if (c_info.m_icKind == GetByIdICInfo::ICKind::OutlinedStorage)
        {
            int32_t c_slot = c_info.m_slot;
            return ic->Effect([base, c_slot, c_mayHaveMt] {
                IcSpecializeValueFullCoverage(c_mayHaveMt, false, true);
                IcSpecifyCaptureValueRange(c_slot, Butterfly::x_namedPropOrdinalRangeMin, Butterfly::x_namedPropOrdinalRangeMax);
                return std::make_pair(base->m_butterfly->GetNamedProperty(c_slot), c_mayHaveMt);
            });
        }
        else
        {
            assert(c_info.m_icKind == GetByIdICInfo::ICKind::MustBeNilButUncacheable);
            return std::make_pair(TValue::Nil(), c_mayHaveMt);
        }
    });

    if (likely(!mayHaveMt || !result.Is<tNil>()))
    {
        Return(result);
    }

    EnterSlowPath<CheckMetatableSlowPath>(base);
}

DEEGEN_DEFINE_BYTECODE(GlobalGet)
{
    Operands(
        Constant("index")
    );
    Result(BytecodeValue);
    Implementation(GlobalGetImpl);
    Variant(
        Op("index").IsConstant<tString>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
