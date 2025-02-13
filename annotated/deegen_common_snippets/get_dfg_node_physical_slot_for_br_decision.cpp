#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_state.h"

static size_t DeegenSnippet_GetDfgNodePhysicalSlotForBrDecision(dfg::NodeOperandConfigData* node)
{
    return node->GetBrDecisionPhysicalSlot();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodePhysicalSlotForBrDecision", DeegenSnippet_GetDfgNodePhysicalSlotForBrDecision)
