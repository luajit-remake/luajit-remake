#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

static void NO_RETURN ArithmeticOperationMetamethodCallContinuation(TValue /*lhs*/, TValue /*rhs*/)
{
    Return(GetReturnValue(0));
}

template<LuaMetamethodKind opKind>
static void NO_RETURN ArithmeticOperationImpl(TValue lhs, TValue rhs)
{
    if (likely(lhs.Is<tDouble>() && rhs.Is<tDouble>()))
    {
        double ld = lhs.As<tDouble>();
        double rd = rhs.As<tDouble>();
        double res;
        if constexpr(opKind == LuaMetamethodKind::Add)
        {
            res = ld + rd;
        }
        else if constexpr(opKind == LuaMetamethodKind::Sub)
        {
            res = ld - rd;
        }
        else if constexpr(opKind == LuaMetamethodKind::Mul)
        {
            res = ld * rd;
        }
        else if constexpr(opKind == LuaMetamethodKind::Div)
        {
            res = ld / rd;
        }
        else if constexpr(opKind == LuaMetamethodKind::Mod)
        {
            res = ModulusWithLuaSemantics(ld, rd);
        }
        else
        {
            static_assert(opKind == LuaMetamethodKind::Pow, "unexpected opKind");
            res = math_fast_pow(ld, rd);
        }
        Return(TValue::Create<tDouble>(res));
    }
    else
    {
        TValue metamethod;

        if (likely(lhs.Is<tTable>()))
        {
            TableObject* tableObj = lhs.As<tTable>();
            TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
            if (result.m_result.m_value != 0)
            {
                TableObject* metatable = result.m_result.As<TableObject>();
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(opKind), icInfo /*out*/);
                metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(opKind).As<void>(), icInfo);
                if (likely(!metamethod.Is<tNil>()))
                {
                    goto do_metamethod_call;
                }
            }
        }

        // Handle case that lhs/rhs are number or string that can be converted to number
        //
        {
            DoBinaryOperationConsideringStringConversionResult res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, opKind);
            if (unlikely(res.success))
            {
                Return(TValue::Create<tDouble>(res.result));
            }
        }

        // Now we know we will need to call metamethod, determine the metamethod to call
        //
        // TODO: this could have been better since we already know lhs is not a table with metatable
        //
        metamethod = GetMetamethodForBinaryArithmeticOperation(lhs, rhs, opKind);
        if (metamethod.Is<tNil>())
        {
            // TODO: make this error consistent with Lua
            //
            ThrowError("Invalid types for arithmetic operation");
        }

do_metamethod_call:
        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(metamethod.As<tFunction>(), lhs, rhs, ArithmeticOperationMetamethodCallContinuation);
        }

        FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(metamethod));
        }

        MakeCall(callTarget, metamethod, lhs, rhs, ArithmeticOperationMetamethodCallContinuation);
    }
}

DEEGEN_DEFINE_BYTECODE_TEMPLATE(ArithmeticOperation, LuaMetamethodKind opKind)
{
    Operands(
        BytecodeSlotOrConstant("lhs"),
        BytecodeSlotOrConstant("rhs")
    );
    Result(BytecodeValue);
    Implementation(ArithmeticOperationImpl<opKind>);
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsBytecodeSlot()
    ).EnableHotColdSplitting(
        Op("lhs").HasType<tDoubleNotNaN>(),
        Op("rhs").HasType<tDoubleNotNaN>()
    );
    Variant(
        Op("lhs").IsConstant<tDoubleNotNaN>(),
        Op("rhs").IsBytecodeSlot()
    ).EnableHotColdSplitting(
        Op("rhs").HasType<tDoubleNotNaN>()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tDoubleNotNaN>()
    ).EnableHotColdSplitting(
        Op("lhs").HasType<tDoubleNotNaN>()
    );
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Add, ArithmeticOperation, LuaMetamethodKind::Add);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Sub, ArithmeticOperation, LuaMetamethodKind::Sub);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Mul, ArithmeticOperation, LuaMetamethodKind::Mul);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Div, ArithmeticOperation, LuaMetamethodKind::Div);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Mod, ArithmeticOperation, LuaMetamethodKind::Mod);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Pow, ArithmeticOperation, LuaMetamethodKind::Pow);

DEEGEN_END_BYTECODE_DEFINITIONS
