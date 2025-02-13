#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_state.h"

static size_t DeegenSnippet_GetDfgPhysicalSlotForSSAInput(dfg::NodeOperandConfigData* node, size_t inputOrd)
{
    return node->GetInputOperandPhysicalSlot(inputOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgPhysicalSlotForSSAInput", DeegenSnippet_GetDfgPhysicalSlotForSSAInput)
