#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_GetNumVariadicResults(CoroutineRuntimeContext* coroCtx)
{
    return coroCtx->m_numVariadicRets;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetNumVariadicResults", DeegenSnippet_GetNumVariadicResults)

