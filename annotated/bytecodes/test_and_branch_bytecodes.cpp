#include "api_define_bytecode.h"
#include "deegen_api.h"

#include "runtime_utils.h"

// Perform a conditional branch depending on 'testValue'.
// When 'testForFalsy' is true, the branch is taken if the value is falsy. Otherwise the branch is taken if it is truthy.
//
template<bool testForFalsy>
static void NO_RETURN TestAndBranchOperationImpl(TValue testValue)
{
    bool isTruthy = testValue.IsTruthy();
    bool shouldBranch = isTruthy ^ testForFalsy;
    if (shouldBranch)
    {
        ReturnAndBranch();
    }
    else
    {
        Return();
    }
}

DEEGEN_DEFINE_BYTECODE_TEMPLATE(TestAndBranchOperation, bool testForFalsy)
{
    Operands(
        BytecodeSlot("testValue")
    );
    Result(ConditionalBranch);
    Implementation(TestAndBranchOperationImpl<testForFalsy>);
    Variant();
    DfgVariant();
    RegAllocHint(
        Op("testValue").RegHint(RegHint::GPR)
    );
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfTruthy, TestAndBranchOperation, false /*testForFalsy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfFalsy, TestAndBranchOperation, true /*testForFalsy*/);

// Performs a conditional branch depending on 'testValue', and simutanuously select a value based on whether the branch is taken.
// When 'testForFalsy' is true, the branch is taken if 'testValue' is falsy. Otherwise the branch is taken if it is truthy.
// If the branch is taken, return 'testValue'. Otherwise, return 'defaultValue'.
//
template<bool testForFalsy>
static void NO_RETURN TestSelectAndBranchOperationImpl(TValue testValue, TValue defaultValue)
{
    bool isTruthy = testValue.IsTruthy();
    bool shouldBranch = isTruthy ^ testForFalsy;
    if (shouldBranch)
    {
        ReturnAndBranch(testValue);
    }
    else
    {
        Return(defaultValue);
    }
}

DEEGEN_DEFINE_BYTECODE_TEMPLATE(TestSelectAndBranchOperation, bool testForFalsy)
{
    Operands(
        BytecodeSlot("testValue"),
        // In rare cases LuaJIT parser will generate this bytecode on uninitialized local.
        // Ideally we should fix the parser, but doing so is harder than I expected
        //
        BytecodeSlot("defaultValue").MaybeInvalidBoxedValue()
    );
    Result(BytecodeValue, ConditionalBranch);
    Implementation(TestSelectAndBranchOperationImpl<testForFalsy>);
    Variant();
    DfgVariant();
    TypeDeductionRule(
        [](TypeMask testValue, TypeMask defaultValue) -> TypeMask
        {
            return testValue.m_mask | defaultValue.m_mask;
        });
    RegAllocHint(
        Op("testValue").RegHint(RegHint::GPR)
    );
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(SelectAndBranchIfTruthy, TestSelectAndBranchOperation, false /*testForFalsy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(SelectAndBranchIfFalsy, TestSelectAndBranchOperation, true /*testForFalsy*/);

DEEGEN_END_BYTECODE_DEFINITIONS
