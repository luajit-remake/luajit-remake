#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN GlobalPutMetamethodCallContinuation(TValue /*tvIndex*/, TValue /*valueToPut*/)
{
    Return();
}

static void NO_RETURN GlobalPutImpl(TValue tvIndex, TValue valueToPut)
{
    assert(tvIndex.Is<tString>());
    HeapPtr<HeapString> index = tvIndex.As<tString>();
    HeapPtr<TableObject> base = GetFEnvGlobalObject();

    TValue metamethod;
    TValue metamethodBase;

retry:
    PutByIdICInfo icInfo;
    TableObject::PreparePutById(base, UserHeapPointer<HeapString> { index }, icInfo /*out*/);

    if (unlikely(TableObject::PutByIdNeedToCheckMetatable(base, icInfo)))
    {
        TableObject::GetMetatableResult gmr = TableObject::GetMetatable(base);
        if (gmr.m_result.m_value != 0)
        {
            HeapPtr<TableObject> metatable = gmr.m_result.As<TableObject>();
            if (unlikely(!TableObject::TryQuicklyRuleOutMetamethod(metatable, LuaMetamethodKind::NewIndex)))
            {
                metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::NewIndex);
                if (!metamethod.Is<tNil>())
                {
                    metamethodBase = TValue::Create<tTable>(base);
                    goto handle_metamethod;
                }
            }
        }
    }

    TableObject::PutById(base, index, valueToPut, icInfo);
    Return();

handle_metamethod:
    // If 'metamethod' is a function, we should invoke the metamethod, throwing out an error if fail
    // Otherwise, we should repeat operation on 'metamethod' (i.e., recurse on metamethod[index])
    //
    if (likely(metamethod.Is<tHeapEntity>()))
    {
        HeapEntityType mmType = metamethod.GetHeapEntityType();
        if (mmType == HeapEntityType::Function)
        {
            MakeCall(metamethod.As<tFunction>(), metamethodBase, tvIndex, valueToPut, GlobalPutMetamethodCallContinuation);
        }
        else if (mmType == HeapEntityType::Table)
        {
            base = metamethod.As<tTable>();
            goto retry;
        }
    }

    // Now we know 'metamethod' is not a function or pointer, so we should locate its own exotic '__index' metamethod..
    // The difference is that if the metamethod is nil, we need to throw an error
    //
    metamethodBase = metamethod;
    metamethod = GetMetamethodForValue(metamethod, LuaMetamethodKind::NewIndex);
    if (metamethod.Is<tNil>())
    {
        // TODO: make error message consistent with Lua
        //
        ThrowError("bad type for GlobalPut");
    }
    goto handle_metamethod;
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
}

DEEGEN_END_BYTECODE_DEFINITIONS
