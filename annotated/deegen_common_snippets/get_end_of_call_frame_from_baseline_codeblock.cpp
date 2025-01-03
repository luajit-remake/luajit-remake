#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t* DeegenSnippet_GetEndOfCallFrameFromBaselineCodeBlock(uint64_t* stackBase, BaselineCodeBlock* bcb)
{
    return stackBase + bcb->m_stackFrameNumSlots;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetEndOfCallFrameFromBaselineCodeBlock", DeegenSnippet_GetEndOfCallFrameFromBaselineCodeBlock)

