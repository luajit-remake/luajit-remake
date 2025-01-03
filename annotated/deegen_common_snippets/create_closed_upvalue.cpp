#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static Upvalue* DeegenSnippet_CreateClosedUpvalue(TValue value)
{
    return Upvalue::CreateClosed(VM::GetActiveVMForCurrentThread(), value);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateClosedUpvalue", DeegenSnippet_CreateClosedUpvalue)
