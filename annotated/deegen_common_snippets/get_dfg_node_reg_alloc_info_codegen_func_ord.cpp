#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_codegen_state.h"

static uint16_t DeegenSnippet_GetDfgNodeOperandConfigDataCodegenFuncOrd(dfg::NodeOperandConfigData* node)
{
    return static_cast<uint16_t>(node->GetCodegenFuncOrd());
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodeOperandConfigDataCodegenFuncOrd", DeegenSnippet_GetDfgNodeOperandConfigDataCodegenFuncOrd)
