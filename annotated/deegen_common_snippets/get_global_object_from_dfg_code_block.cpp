#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static HeapPtr<TableObject> DeegenSnippet_GetGlobalObjectFromDfgCodeBlock(DfgCodeBlock* dcb)
{
    return dcb->m_globalObject.As();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetGlobalObjectFromDfgCodeBlock", DeegenSnippet_GetGlobalObjectFromDfgCodeBlock)

