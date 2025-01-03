#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_node_info.h"

static size_t DeegenSnippet_GetDfgPhysicalSlotForSSAInput(dfg::NodeRegAllocInfo* node, size_t inputOrd)
{
    return node->GetInputRAInfo(inputOrd)->GetPhysicalSlot();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgPhysicalSlotForSSAInput", DeegenSnippet_GetDfgPhysicalSlotForSSAInput)
