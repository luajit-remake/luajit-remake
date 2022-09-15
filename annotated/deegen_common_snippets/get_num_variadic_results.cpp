#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static uint32_t DeegenSnippet_GetNumVariadicResults(CoroutineRuntimeContext* coroCtx)
{
    return coroCtx->m_numVariadicRets;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetNumVariadicResults", DeegenSnippet_GetNumVariadicResults)

