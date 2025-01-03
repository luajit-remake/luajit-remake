#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static int64_t DeegenSnippet_GetInterpreterTierUpCounter(CodeBlock* cb)
{
    return cb->m_interpreterTierUpCounter;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetInterpreterTierUpCounter", DeegenSnippet_GetInterpreterTierUpCounter)
