#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_state.h"

static size_t DeegenSnippet_GetDfgNodePhysicalSlotStartForRangeOperand(dfg::NodeOperandConfigData* node)
{
    return node->GetRangeOperandPhysicalSlotStart();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodePhysicalSlotStartForRangeOperand", DeegenSnippet_GetDfgNodePhysicalSlotStartForRangeOperand)
