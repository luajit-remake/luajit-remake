#include "define_deegen_common_snippet.h"
#include "drt/dfg_codegen_protocol.h"

static uint64_t DeegenSnippet_GetDfgBuiltinNodeOutputPhysicalSlot(dfg::BuiltinNodeOperandsInfoBase* info)
{
    return info->GetOutputPhysicalSlot();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgBuiltinNodeOutputPhysicalSlot", DeegenSnippet_GetDfgBuiltinNodeOutputPhysicalSlot)
