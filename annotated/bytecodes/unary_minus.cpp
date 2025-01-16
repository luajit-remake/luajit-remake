#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

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
    TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Unm);

    if (likely(metamethod.Is<tFunction>()))
    {
        MakeCall(metamethod.As<tFunction>(), input, input, UnaryMinusMetamethodCallContinuation);
    }

    if (unlikely(metamethod.Is<tNil>()))
    {
        // TODO: make this error consistent with Lua
        //
        ThrowError("Invalid type for unary minus");
    }

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(metamethod);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    MakeCall(callTarget, metamethod, input, input, UnaryMinusMetamethodCallContinuation);
}

DEEGEN_DEFINE_BYTECODE(UnaryMinus)
{
    Operands(
        BytecodeSlot("input")
    );
    Result(BytecodeValue);
    Implementation(UnaryMinusImpl);
    Variant().EnableHotColdSplitting(
        Op("input").HasType<tDouble>()
    );
    DfgVariant(
        Op("input").HasType<tDouble>()
    );
    DfgVariant().EnableHotColdSplitting(
        Op("input").HasType<tDouble>()
    );
    TypeDeductionRule(
        [](TypeMask input) -> TypeMask
        {
            if (input.SubsetOf<tDouble>())
            {
                return x_typeMaskFor<tDouble>;
            }
            else
            {
                return x_typeMaskFor<tBoxedValueTop>;
            }
        });
    RegAllocHint(
        Op("input").RegHint(RegHint::FPR),
        Op("output").RegHint(RegHint::FPR)
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
