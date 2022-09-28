#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

static void NO_RETURN ArithmeticOperationMetamethodCallContinuation(TValue /*lhs*/, TValue /*rhs*/)
{
    Return(GetReturnValue(0));
}

static void NO_RETURN ArithmeticAdd(TValue lhs, TValue rhs)
{
    if (likely(lhs.Is<tDouble>() && rhs.Is<tDouble>()))
    {
        double res =lhs.As<tDouble>() + rhs.As<tDouble>();
        Return(TValue::Create<tDouble>(res));
    }
    else
    {
        TValue metamethod;

        if (likely(lhs.Is<tTable>()))
        {
            HeapPtr<TableObject> tableObj = lhs.As<tTable>();
            TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
            if (result.m_result.m_value != 0)
            {
                HeapPtr<TableObject> metatable = result.m_result.As<TableObject>();
                GetByIdICInfo icInfo;
                TableObject::PrepareGetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Add), icInfo /*out*/);
                metamethod = TableObject::GetById(metatable, VM_GetStringNameForMetatableKind(LuaMetamethodKind::Add).As<void>(), icInfo);
                if (likely(!metamethod.IsNil()))
                {
                    goto do_metamethod_call;
                }
            }
        }

        // Handle case that lhs/rhs are number or string that can be converted to number
        //
        {
            std::optional<double> res = TryDoBinaryOperationConsideringStringConversion(lhs, rhs, [](double l, double r) { return l + r; });
            if (res)
            {
                Return(TValue::CreateDouble(res.value()));
            }
        }

        // Now we know we will need to call metamethod, determine the metamethod to call
        //
        // TODO: this could have been better since we already know lhs is not a table with metatable
        //
        metamethod = GetMetamethodForBinaryArithmeticOperation<LuaMetamethodKind::Add>(lhs, rhs);
        if (metamethod.IsNil())
        {
            // TODO: make this error consistent with Lua
            //
            ThrowError("Invalid types for arithmetic add");
        }

do_metamethod_call:
        {
            GetCallTargetConsideringMetatableResult callTarget = GetCallTargetConsideringMetatable(metamethod);
            if (callTarget.m_target.m_value == 0)
            {
                ThrowError(MakeErrorMessageForUnableToCall(metamethod));
            }

            if (unlikely(callTarget.m_invokedThroughMetatable))
            {
                MakeCall(callTarget.m_target.As(), metamethod, lhs, rhs, ArithmeticOperationMetamethodCallContinuation);
            }
            else
            {
                MakeCall(callTarget.m_target.As(), lhs, rhs, ArithmeticOperationMetamethodCallContinuation);
            }
        }
    }
}

DEEGEN_DEFINE_BYTECODE(ArithmeticAdd)
{
    Operands(
        BytecodeSlotOrConstant("lhs"),
        BytecodeSlotOrConstant("rhs")
    );
    Result(BytecodeValue);
    Implementation(ArithmeticAdd);
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsBytecodeSlot()
    );
    Variant(
        Op("lhs").IsConstant<tDouble>(),
        Op("rhs").IsBytecodeSlot()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tDouble>()
    );
}

DEEGEN_END_BYTECODE_DEFINITIONS
