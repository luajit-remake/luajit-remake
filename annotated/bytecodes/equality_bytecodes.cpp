#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

namespace {

template<bool compareForNotEqual, bool shouldBranch>
void NO_RETURN EqualityOperationMetamethodCallContinuation(TValue /*lhs*/, TValue /*rhs*/)
{
    TValue retValue = GetReturnValue(0);
    bool isTruthy = retValue.IsTruthy();
    bool result = isTruthy ^ compareForNotEqual;
    if constexpr(shouldBranch)
    {
        if (result) { ReturnAndBranch(); } else { Return(); }
    }
    else
    {
        Return(TValue::Create<tBool>(result));
    }
}

// When 'compareForNotEqual' is true, it computes !(lhs == rhs). Otherwise it computes lhs == rhs
// When 'shouldBranch' is true, it performs a branch if the computation is true. Otherwise it simply returns the computation result.
//
template<bool compareForNotEqual, bool shouldBranch>
void NO_RETURN EqualityOperationImpl(TValue lhs, TValue rhs)
{
    bool result;
    // This is slightly tricky. We check 'rhs' first because the variants are specializing on 'rhs'.
    // Note that in theory, we could have made our TValue typecheck elimination supported "bitwise-equal" comparsion
    // (to optimize the "lhs.m_value == rhs.m_value" check a few lines below based on known type information).
    // If we had implemented that, then it would no longer matter whether we check 'lhs' first or 'rhs' first,
    // as our TValue typecheck elimination would give us the same optimal result in both cases.
    // However, we haven't implemented it yet, and even though it is not hard to implement it, for now let's just stay simple.
    //
    if (likely(rhs.Is<tDouble>()))
    {
        // When RHS is a double, we can execute the comparison by simply doing 'lhs.ViewAsDouble() == rhs.As<tDouble>()'.
        // Here is why it is correct: since RHS is a double,
        // (1) If LHS is not a double, the equality is always false according to Lua rule.
        // (2) If LHS is a double NaN, the equality is always false because NaN is not equal to any double.
        //
        // And when the LHS TValue is *viewed* as a double, all non-double value will become double NaN:
        //     double -> double, comparison with RHS works as expected
        //     double NaN -> double NaN, comparison with RHS yields false as expected
        //     non-double -> double NaN, comparison with RHS yields false as expected
        //
        // In all three cases, doing a double-comparison with RHS yields the same result as Lua rule expects.
        //
        bool isEqualAsDouble = UnsafeFloatEqual(lhs.ViewAsDouble(), rhs.As<tDouble>());
        result = isEqualAsDouble ^ compareForNotEqual;
        goto end;
    }

    if (lhs.m_value == rhs.m_value)
    {
        result = true ^ compareForNotEqual;
        goto end;
    }

    assert(!lhs.Is<tInt32>() && "unimplemented");
    assert(!rhs.Is<tInt32>() && "unimplemented");

    if (likely(lhs.Is<tTable>() && rhs.Is<tTable>()))
    {
        // Consider metamethod call
        //
        HeapPtr<TableObject> lhsMetatable;
        {
            HeapPtr<TableObject> tableObj = lhs.As<tTable>();
            TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
            if (gmr.m_result.m_value == 0)
            {
                goto not_equal;
            }
            lhsMetatable = gmr.m_result.As<TableObject>();
        }

        HeapPtr<TableObject> rhsMetatable;
        {
            HeapPtr<TableObject> tableObj = rhs.As<tTable>();
            TableObject::GetMetatableResult gmr = TableObject::GetMetatable(tableObj);
            if (gmr.m_result.m_value == 0)
            {
                goto not_equal;
            }
            rhsMetatable = gmr.m_result.As<TableObject>();
        }

        TValue metamethod = GetMetamethodFromMetatableForComparisonOperation<true /*supportsQuicklyRuleOutMM*/>(lhsMetatable, rhsMetatable, LuaMetamethodKind::Eq);
        if (likely(metamethod.Is<tNil>()))
        {
            goto not_equal;
        }

        if (likely(metamethod.Is<tFunction>()))
        {
            MakeCall(TranslateToRawPointer(metamethod.As<tFunction>()), lhs, rhs, EqualityOperationMetamethodCallContinuation<compareForNotEqual, shouldBranch>);
        }

        FunctionObject* callTarget = GetCallTargetViaMetatable(metamethod);
        if (unlikely(callTarget == nullptr))
        {
            ThrowError(MakeErrorMessageForUnableToCall(metamethod));
        }

        MakeCall(callTarget, metamethod, lhs, rhs, EqualityOperationMetamethodCallContinuation<compareForNotEqual, shouldBranch>);
    }

not_equal:
    result = false ^ compareForNotEqual;

end:
    if constexpr(shouldBranch)
    {
        if (result) { ReturnAndBranch(); } else { Return(); }
    }
    else
    {
        Return(TValue::Create<tBool>(result));
    }
}

}   // anonymous namespace

DEEGEN_DEFINE_BYTECODE_TEMPLATE(EqualityOperation, bool compareForNotEqual, bool shouldBranch)
{
    Operands(
        BytecodeSlotOrConstant("lhs"),
        BytecodeSlotOrConstant("rhs")
    );
    Result(shouldBranch ? ConditionalBranch : BytecodeValue);
    Implementation(EqualityOperationImpl<compareForNotEqual, shouldBranch>);
    // TODO: we need quickening since we want to dynamically specialize for double, not-double, and maybe table
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsBytecodeSlot()
    ).EnableHotColdSplitting(
        Op("lhs").HasType<tDoubleNotNaN>(),
        Op("rhs").HasType<tDoubleNotNaN>()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tDouble>()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tString>()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tNil>()
    );
    Variant(
        Op("lhs").IsBytecodeSlot(),
        Op("rhs").IsConstant<tBool>()
    );
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfEq, EqualityOperation, false /*compareForNotEqual*/, true /*shouldBranch*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfNotEq, EqualityOperation, true /*compareForNotEqual*/, true /*shouldBranch*/);

DEEGEN_END_BYTECODE_DEFINITIONS
