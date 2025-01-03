#include "define_deegen_common_snippet.h"
#include "drt/dfg_codegen_protocol.h"

static uint64_t DeegenSnippet_GetDfgBuiltinNodeInputPhysicalSlot(dfg::BuiltinNodeOperandsInfoBase* info, uint64_t inputOrd)
{
    return info->GetInputPhysicalSlot(inputOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgBuiltinNodeInputPhysicalSlot", DeegenSnippet_GetDfgBuiltinNodeInputPhysicalSlot)
