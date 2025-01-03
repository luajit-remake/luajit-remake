#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_node_info.h"

static size_t DeegenSnippet_GetDfgNodePhysicalSlotStartForRangeOperand(dfg::NodeRegAllocInfo* node)
{
    return node->GetRangeOperandPhysicalSlotStart();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodePhysicalSlotStartForRangeOperand", DeegenSnippet_GetDfgNodePhysicalSlotStartForRangeOperand)
