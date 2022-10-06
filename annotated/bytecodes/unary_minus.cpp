#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN UnaryMinusMetamethodCallContinuation(TValue /*input*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN UnaryMinusImpl(TValue input)
{
    if (likely(input.Is<tDouble>()))
    {
        double result = -input.As<tDouble>();
        Return(TValue::Create<tDouble>(result));
    }

    if (input.Is<tString>())
    {
        HeapPtr<HeapString> stringObj = input.As<tString>();
        StrScanResult ssr = TryConvertStringToDoubleWithLuaSemantics(TranslateToRawPointer(stringObj->m_string), stringObj->m_length);
        if (ssr.fmt == StrScanFmt::STRSCAN_NUM)
        {
            double result = -ssr.d;
            Return(TValue::Create<tDouble>(result));
        }
    }

    UserHeapPointer<void> metatableMaybeNull = GetMetatableForValue(input);
    if (metatableMaybeNull.m_value == 0)
    {
        ThrowError("Invalid types for unary minus");
    }

    HeapPtr<TableObject> metatable = metatableMaybeNull.As<TableObject>();
    GetByIdICInfo icInfo;
    TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Unm), icInfo /*out*/);
    TValue metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Unm).As<void>(), icInfo);

    if (metamethod.Is<tNil>())
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid type for unary minus");
    }

    GetCallTargetConsideringMetatableResult callTarget = GetCallTargetConsideringMetatable(metamethod);
    if (callTarget.m_target.m_value == 0)
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    if (unlikely(callTarget.m_invokedThroughMetatable))
    {
        MakeCall(callTarget.m_target.As(), metamethod, input, input, UnaryMinusMetamethodCallContinuation);
    }
    else
    {
        MakeCall(callTarget.m_target.As(), input, input, UnaryMinusMetamethodCallContinuation);
    }
}

DEEGEN_DEFINE_BYTECODE(UnaryMinus)
{
    Operands(
        BytecodeSlot("input")
    );
    Result(BytecodeValue);
    Implementation(UnaryMinusImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
