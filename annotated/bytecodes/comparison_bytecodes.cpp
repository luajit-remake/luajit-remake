#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

namespace {

enum class ComparatorKind
{
    LessThan,
    NotLessThan,
    LessEqual,
    NotLessEqual
};

// Compare two double values
//
template<ComparatorKind kind>
bool ALWAYS_INLINE DoComparison(double lhs, double rhs)
{
    if constexpr(kind == ComparatorKind::LessThan)
    {
        return lhs < rhs;
    }
    else if constexpr(kind == ComparatorKind::NotLessThan)
    {
        // Note that NotLessThan is different from GreaterEqual in the presense of NaN
        // Same for NotLessEqual below
        //
        return !(lhs < rhs);
    }
    else if constexpr(kind == ComparatorKind::LessEqual)
    {
        return lhs <= rhs;
    }
    else
    {
        static_assert(kind == ComparatorKind::NotLessEqual);
        return !(lhs <= rhs);
    }
}

// Compare two string values
//
template<ComparatorKind kind>
bool ALWAYS_INLINE DoComparison(HeapString* lhs, HeapString* rhs)
{
    int cmpRes = lhs->Compare(rhs);
    if constexpr(kind == ComparatorKind::LessThan)
    {
        return cmpRes < 0;
    }
    else if constexpr(kind == ComparatorKind::NotLessThan)
    {
        return cmpRes >= 0;
    }
    else if constexpr(kind == ComparatorKind::LessEqual)
    {
        return cmpRes <= 0;
    }
    else
    {
        static_assert(kind == ComparatorKind::NotLessEqual);
        return cmpRes > 0;
    }
}

// Get the kind of Lua metamethod to call for the given comparison operator
//
template<ComparatorKind kind>
consteval LuaMetamethodKind GetMetamethodKind()
{
    if constexpr(kind == ComparatorKind::LessThan || kind == ComparatorKind::NotLessThan)
    {
        return LuaMetamethodKind::Lt;
    }
    else
    {
        static_assert(kind == ComparatorKind::LessEqual || kind == ComparatorKind::NotLessEqual);
        return LuaMetamethodKind::Le;
    }
}

template<ComparatorKind kind>
consteval bool ShouldInvertMetatableCallResult()
{
    return kind == ComparatorKind::NotLessThan || kind == ComparatorKind::NotLessEqual;
}

template<bool shouldBranch, bool shouldInvertResult>
void NO_RETURN ComparisonOperationMetamethodCallContinuation(TValue /*lhs*/, TValue /*rhs*/)
{
    TValue returnedVal = GetReturnValue(0);
    bool isTruthy = returnedVal.IsTruthy();
    bool res = isTruthy ^ shouldInvertResult;
    if constexpr(shouldBranch)
    {
        if (res) { ReturnAndBranch(); } else { Return(); }
    }
    else
    {
        Return(TValue::Create<tBool>(res));
    }
}

template<bool shouldBranch, ComparatorKind opKind>
void NO_RETURN ComparisonOperationImpl(TValue lhs, TValue rhs)
{
    if (likely(lhs.Is<tDouble>()))
    {
        if (unlikely(!rhs.Is<tDouble>()))
        {
            goto fail;
        }
        bool result = DoComparison<opKind>(lhs.As<tDouble>(), rhs.As<tDouble>());
        if constexpr(shouldBranch)
        {
            if (result) { ReturnAndBranch(); } else { Return(); }
        }
        else
        {
            Return(TValue::Create<tBool>(result));
        }
    }
    else
    {
        TValue metamethod;
        if (lhs.Is<tHeapEntity>())
        {
            if (!rhs.Is<tHeapEntity>())
            {
                goto fail;
            }
            HeapEntityType lhsTy = lhs.GetHeapEntityType();
            HeapEntityType rhsTy = rhs.GetHeapEntityType();
            if (unlikely(lhsTy != rhsTy))
            {
                goto fail;
            }

            if (lhs.Is<tString>())
            {
                VM* vm = VM::GetActiveVMForCurrentThread();
                HeapString* lhsString = TranslateToRawPointer(vm, lhs.As<tString>());
                HeapString* rhsString = TranslateToRawPointer(vm, rhs.As<tString>());
                bool result = DoComparison<opKind>(lhsString, rhsString);
                if constexpr(shouldBranch)
                {
                    if (result) { ReturnAndBranch(); } else { Return(); }
                }
                else
                {
                    Return(TValue::Create<tBool>(result));
                }
            }

            if (lhs.Is<tTable>())
            {
                HeapPtr<TableObject> lhsMetatable;
                {
                    HeapPtr<TableObject> tableObj = lhs.As<tTable>();
                    TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                    if (result.m_result.m_value == 0)
                    {
                        goto fail;
                    }
                    lhsMetatable = result.m_result.As<TableObject>();
                }

                HeapPtr<TableObject> rhsMetatable;
                {
                    HeapPtr<TableObject> tableObj = rhs.As<tTable>();
                    TableObject::GetMetatableResult result = TableObject::GetMetatable(tableObj);
                    if (result.m_result.m_value == 0)
                    {
                        goto fail;
                    }
                    rhsMetatable = result.m_result.As<TableObject>();
                }

                metamethod = GetMetamethodFromMetatableForComparisonOperation<GetMetamethodKind<opKind>()>(lhsMetatable, rhsMetatable);
                if (metamethod.IsNil())
                {
                    goto fail;
                }
                goto do_metamethod_call;
            }

            assert(!lhs.Is<tUserdata>() && "unimplemented");

            metamethod = GetMetamethodForValue(lhs, GetMetamethodKind<opKind>());
            if (metamethod.IsNil())
            {
                goto fail;
            }
            goto do_metamethod_call;
        }

        assert(!lhs.Is<tInt32>() && "unimplemented");

        {
            assert(lhs.Is<tMIV>());
            if (!rhs.Is<tMIV>())
            {
                goto fail;
            }
            // Must be both 'nil', or both 'boolean', in order to consider metatable
            //
            if (lhs.Is<tNil>() != rhs.Is<tNil>())
            {
                goto fail;
            }
            metamethod = GetMetamethodForValue(lhs, GetMetamethodKind<opKind>());
            if (metamethod.IsNil())
            {
                goto fail;
            }
            goto do_metamethod_call;
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
                MakeCall(callTarget.m_target.As(), metamethod, lhs, rhs, ComparisonOperationMetamethodCallContinuation<shouldBranch, ShouldInvertMetatableCallResult<opKind>()>);
            }
            else
            {
                MakeCall(callTarget.m_target.As(), lhs, rhs, ComparisonOperationMetamethodCallContinuation<shouldBranch, ShouldInvertMetatableCallResult<opKind>()>);
            }
        }
    }
fail:
    // TODO: make this error consistent with Lua
    //
    ThrowError("Invalid types for comparison operator");
}

}   // anonymous namespace

DEEGEN_DEFINE_BYTECODE_TEMPLATE(ComparisonOperation, bool shouldBranch, ComparatorKind opKind)
{
    Operands(
        BytecodeSlotOrConstant("lhs"),
        BytecodeSlotOrConstant("rhs")
    );
    Result(shouldBranch ? ConditionalBranch : BytecodeValue);
    Implementation(ComparisonOperationImpl<shouldBranch, opKind>);
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

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfLT, ComparisonOperation, true /*shouldBranch*/, ComparatorKind::LessThan);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfNLT, ComparisonOperation, true /*shouldBranch*/, ComparatorKind::NotLessThan);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfLE, ComparisonOperation, true /*shouldBranch*/, ComparatorKind::LessEqual);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfNLE, ComparisonOperation, true /*shouldBranch*/, ComparatorKind::NotLessEqual);

DEEGEN_END_BYTECODE_DEFINITIONS
