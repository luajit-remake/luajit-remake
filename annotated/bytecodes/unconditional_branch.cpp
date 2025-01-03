#include "api_define_bytecode.h"
#include "deegen_api.h"

static void NO_RETURN UnconditionalBranchImpl()
{
    ReturnAndBranch();
}

// 'isLoopHint' hints that whether this jump is a loop back edge
//
DEEGEN_DEFINE_BYTECODE_TEMPLATE(BranchOperation, bool isLoopHint)
{
    Operands();
    Result(ConditionalBranch);
    Implementation(UnconditionalBranchImpl);
    CheckForInterpreterTierUp(isLoopHint);
    Variant();
    DfgVariant();
}

DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(Branch, BranchOperation, false /*isLoopHint*/);
DEEGEN_DEFINE_BYTECODE_BY_TEMPLATE_INSTANTIATION(BranchLoopHint, BranchOperation, true /*isLoopHint*/);

// This is a NO-OP, just a hint that this is a loop header so one should tier-up from here
//
static void NO_RETURN LoopHeaderHintImpl()
{
    Return();
}

DEEGEN_DEFINE_BYTECODE(LoopHeaderHint)
{
    Operands();
    Result(NoOutput);
    Implementation(LoopHeaderHintImpl);
    CheckForInterpreterTierUp(true);
    Variant();
    DfgVariant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
