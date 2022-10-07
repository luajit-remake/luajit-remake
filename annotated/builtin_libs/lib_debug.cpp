#include "deegen_api.h"
#include "runtime_utils.h"

// Lua standard library debug.getmetatable
//
DEEGEN_DEFINE_LIB_FUNC(debug_getmetatable)
{
    if (GetNumArgs() == 0)
    {
        ThrowError("bad argument #1 to 'getmetatable' (value expected)");
    }

    TValue value = GetArg(0);
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(value);
    if (metatableMaybeNull.m_value == 0)
    {
        Return(TValue::Create<tNil>());
    }
    else
    {
        Return(TValue::Create<tHeapEntity>(metatableMaybeNull.As()));
    }
}

// Lua standard library debug.setmetatable
//
DEEGEN_DEFINE_LIB_FUNC(debug_setmetatable)
{
    size_t numArgs = GetNumArgs();
    if (numArgs <= 1)
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    TValue value = GetArg(0);
    TValue mt = GetArg(1);
    if (!mt.Is<tNil>() && !mt.Is<tTable>())
    {
        ThrowError("bad argument #2 to 'setmetatable' (nil or table expected)");
    }

    auto setExoticMetatable = [&](UserHeapPointer<void>& metatable)
    {
        if (mt.IsNil())
        {
            metatable = UserHeapPointer<void>();
        }
        else
        {
            metatable = mt.As<tHeapEntity>();
        }
    };

    VM* vm = VM::GetActiveVMForCurrentThread();
    if (value.Is<tHeapEntity>())
    {
        HeapEntityType ty = value.GetHeapEntityType();

        if (ty == HeapEntityType::Table)
        {
            TableObject* obj = TranslateToRawPointer(vm, value.As<tTable>());
            if (!mt.Is<tNil>())
            {
                obj->SetMetatable(vm, mt.As<tTable>());
            }
            else
            {
                obj->RemoveMetatable(vm);
            }
        }
        else if (ty == HeapEntityType::String)
        {
            setExoticMetatable(vm->m_metatableForString);
        }
        else if (ty == HeapEntityType::Function)
        {
            setExoticMetatable(vm->m_metatableForFunction);
        }
        else if (ty == HeapEntityType::Thread)
        {
            setExoticMetatable(vm->m_metatableForCoroutine);
        }
        else
        {
            // TODO: support USERDATA
            ReleaseAssert(false && "unimplemented");
        }
    }
    else if (value.Is<tMIV>())
    {
        if (value.Is<tNil>())
        {
            setExoticMetatable(vm->m_metatableForNil);
        }
        else
        {
            assert(value.Is<tBool>());
            setExoticMetatable(vm->m_metatableForBoolean);
        }
    }
    else
    {
        assert(value.Is<tDouble>() || value.Is<tInt32>());
        setExoticMetatable(vm->m_metatableForNumber);
    }

    // The return value of 'debug.setmetatable' is 'true' in Lua 5.1, but the original object in Lua 5.2 and higher
    // For now we implement Lua 5.1 behavior
    //
    Return(TValue::Create<tBool>(true));
}

DEEGEN_END_LIB_FUNC_DEFINITIONS
