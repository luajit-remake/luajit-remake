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
    TValue metamethod = GetMetamethodFromMetatable(metatable, LuaMetamethodKind::Len);

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
        MakeCall(metamethod.As<tFunction>(), input, LengthOperatorMetamethodCallContinuation);
    }

    HeapPtr<FunctionObject> callTarget = GetCallTargetViaMetatable(metamethod);
    if (unlikely(callTarget == nullptr))
    {
        ThrowError(MakeErrorMessageForUnableToCall(metamethod));
    }

    MakeCall(callTarget, metamethod, input, LengthOperatorMetamethodCallContinuation);
}

static void NO_RETURN LengthOperatorTableLengthSlowPath(TValue input)
{
    Assert(input.Is<tTable>());
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
    Variant();
    DfgVariant(Op("input").HasType<tString>());
    DfgVariant(Op("input").HasType<tTable>());
    DfgVariant();
    // Output type deduction: if input is string, output is always double. Otherwise cannot reason about.
    // But it's also kind of important to be able to speculate double, so we will value profile as well.
    //
    TypeDeductionRule(
        [](TypeMask input) -> TypeMask
        {
            if (input.SubsetOf<tString>())
            {
                return x_typeMaskFor<tDouble>;
            }
            else
            {
                return x_typeMaskFor<tBoxedValueTop>;
            }
        });
    TypeDeductionRule(ValueProfile);
    RegAllocHint(
        Op("input").RegHint(RegHint::GPR),
        Op("output").RegHint(RegHint::FPR)
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
