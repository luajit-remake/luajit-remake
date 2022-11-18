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
        BytecodeSlot("defaultValue")
    );
    Result(BytecodeValue, ConditionalBranch);
    Implementation(TestSelectAndBranchOperationImpl<testForFalsy>);
    Variant();
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(SelectAndBranchIfTruthy, TestSelectAndBranchOperation, false /*testForFalsy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(SelectAndBranchIfFalsy, TestSelectAndBranchOperation, true /*testForFalsy*/);

// An unconditional branch. This bytecode has identical functionality as the 'Branch' bytecode,
// but is enforced to have a bytecode length identical to the above CondBr/SelectAndCondBr bytecodes.
// This allows the bytecode builder frontend can safely replace one of the above bytecodes to an
// unconditional branch, without breaking the bytecode stream.
//
static void NO_RETURN UncondBrReducedFromCondBrImpl()
{
    ReturnAndBranch();
}

DEEGEN_DEFINE_BYTECODE(UncondBrReducedFromCondBr)
{
    Operands();
    Result(ConditionalBranch);
    Implementation(UncondBrReducedFromCondBrImpl);
    Variant();
}

// BranchIfFalsy, BranchIfTruthy, SelectAndBranchIfTruthy, SelectAndBranchIfFalsy and UncondBrReducedFromCondBr must have the same length,
// as the bytecode builder frontend needs to late-replace a 'SelectAndCondBr' with a 'CondBr', or replace a 'CondBr' with a 'UncondBr',
// or to flip the branch condition.
//
DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(BranchIfFalsy, BranchIfTruthy);
DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(BranchIfFalsy, SelectAndBranchIfTruthy);
DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(BranchIfFalsy, SelectAndBranchIfFalsy);
DEEGEN_ADD_BYTECODE_SAME_LENGTH_CONSTRAINT(BranchIfFalsy, UncondBrReducedFromCondBr);

DEEGEN_END_BYTECODE_DEFINITIONS
