#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t* DeegenSnippet_GetEndOfCallFrameFromDfgCodeBlock(uint64_t* stackBase, DfgCodeBlock* dcb)
{
    return stackBase + dcb->m_stackFrameNumSlots;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetEndOfCallFrameFromDfgCodeBlock", DeegenSnippet_GetEndOfCallFrameFromDfgCodeBlock)

