#include "define_deegen_common_snippet.h"
#include "dfg_reg_alloc_node_info.h"

static uint16_t DeegenSnippet_GetDfgNodeRegAllocInfoCodegenFuncOrd(dfg::NodeRegAllocInfo* node)
{
    return static_cast<uint16_t>(node->m_codegenFuncOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgNodeRegAllocInfoCodegenFuncOrd", DeegenSnippet_GetDfgNodeRegAllocInfoCodegenFuncOrd)
