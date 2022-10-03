#include "bytecode_definition_utils.h"
#include "deegen_api.h"

static void NO_RETURN UnconditionalBranchImpl()
{
    ReturnAndBranch();
}

DEEGEN_DEFINE_BYTECODE(Branch)
{
    Operands();
    Result(ConditionalBranch);
    Implementation(UnconditionalBranchImpl);
    Variant();
}

DEEGEN_END_BYTECODE_DEFINITIONS
