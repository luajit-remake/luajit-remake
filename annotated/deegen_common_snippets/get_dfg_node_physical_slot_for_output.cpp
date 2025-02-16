#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_codegen_state.h"

static size_t DeegenSnippet_GetDfgNodePhysicalSlotForOutput(dfg::NodeOperandConfigData* node)
{
    return node->GetOutputPhysicalSlot();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodePhysicalSlotForOutput", DeegenSnippet_GetDfgNodePhysicalSlotForOutput)
