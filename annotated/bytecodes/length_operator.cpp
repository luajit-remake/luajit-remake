#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN LengthOperatorMetamethodCallContinuation(TValue /*input*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN LengthOperatorNotTableOrStringSlowPath(TValue input)
{
    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(input);
    if (metatableMaybeNull.m_value == 0)
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid types for length");
    }

    HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Len), icInfo /*out*/);
    TValue metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Len).As<void>(), icInfo);

    if (metamethod.Is<tNil>())
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid types for length");
    }

    // Lua idiosyncrasy: in Lua 5.1, for unary minus, the parameter passed to metamethod is 'input, input',
    // but for 'length', the parameter is only 'input'.
    // Lua 5.2+ unified the behavior and always pass 'input, input', but for now we are targeting Lua 5.1.
    //
    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), input, LengthOperatorMetamethodCallContinuation);
    }

    FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    MakeCall(callTarget, metamethod, input, LengthOperatorMetamethodCallContinuation);
}

static void NO_RETURN LengthOperatorTableLengthSlowPath(TValue input)
{
    assert(input.Is<tTable>());
    uint32_t length = TableObject::GetTableLengthWithLuaSemanticsSlowPath(input.As<tTable>());
    Return(TValue::Create<tDouble>(length));
}

static void NO_RETURN LengthOperatorImpl(TValue input)
{
    if (input.Is<tString>())
    {
        HeapPtr<HeapString> s = input.As<tString>();
        Return(TValue::Create<tDouble>(s->m_length));
    }

    if (likely(input.Is<tTable>()))
    {
        // In Lua 5.1, the primitive length operator is always used, even if there exists a 'length' metamethod
        // But in Lua 5.2+, the 'length' metamethod takes precedence, so this needs to be changed once we add support for Lua 5.2+
        //
        HeapPtr<TableObject> s = input.As<tTable>();
        auto [success, result] = TableObject::TryGetTableLengthWithLuaSemanticsFastPath(s);
        if (likely(success))
        {
            Return(TValue::Create<tDouble>(result));
        }
        else
        {
            EnterSlowPath<LengthOperatorTableLengthSlowPath>();
        }
    }

    EnterSlowPath<LengthOperatorNotTableOrStringSlowPath>();
}

DEEGEN_DEFINE_BYTECODE(LengthOf)
{
    Operands(
        BytecodeSlot("input")
    );
    Result(BytecodeValue);
    Implementation(LengthOperatorImpl);
    // TODO: we need quickening since we want to dynamically specialize for tString and tTable
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
