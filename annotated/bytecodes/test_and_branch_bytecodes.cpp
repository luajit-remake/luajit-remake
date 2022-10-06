#include "bytecode_definition_utils.h"
#include "deegen_api.h"

#include "bytecode.h"

namespace {

// When 'testForFalsy' is true, it branches if the value is falsy. Otherwise it branches if the variable is truthy.
// If 'shouldCopy' is true, it also returns the original value passed in. Note that this implies that the copy unconditionally happens.
//
template<bool testForFalsy, bool shouldCopy>
void NO_RETURN TestAndBranchOperationImpl(TValue value)
{
    bool isTruthy = value.IsTruthy();
    bool shouldBranch = isTruthy ^ testForFalsy;
    if (shouldBranch)
    {
        if constexpr(shouldCopy) { ReturnAndBranch(value); } else { ReturnAndBranch(); }
    }
    else
    {
        if constexpr(shouldCopy) { Return(value); } else { Return(); }
    }
}

}   // anonymous namespace

DEEGEN_DEFINE_BYTECODE_TEMPLATE(TestAndBranchOperation, bool testForFalsy, bool shouldCopy)
{
    Operands(
        BytecodeSlot("value")
    );
    if (shouldCopy)
    {
        Result(BytecodeValue, ConditionalBranch);
    }
    else
    {
        Result(ConditionalBranch);
    }
    Implementation(TestAndBranchOperationImpl<testForFalsy, shouldCopy>);
    Variant();
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfTruthy, TestAndBranchOperation, false /*testForFalsy*/, false /*shouldCopy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchIfFalsy, TestAndBranchOperation, true /*testForFalsy*/, false /*shouldCopy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CopyAndBranchIfTruthy, TestAndBranchOperation, false /*testForFalsy*/, true /*shouldCopy*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(CopyAndBranchIfFalsy, TestAndBranchOperation, true /*testForFalsy*/, true /*shouldCopy*/);

DEEGEN_END_BYTECODE_DEFINITIONS
