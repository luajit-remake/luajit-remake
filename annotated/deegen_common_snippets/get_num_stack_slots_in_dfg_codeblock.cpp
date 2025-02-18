#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_GetNumStackSlotsInDfgCodeBlock(DfgCodeBlock* dcb)
{
    return dcb->m_stackFrameNumSlots;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetNumStackSlotsInDfgCodeBlock", DeegenSnippet_GetNumStackSlotsInDfgCodeBlock)
