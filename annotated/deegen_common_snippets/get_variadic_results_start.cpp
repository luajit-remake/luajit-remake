#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue* DeegenSnippet_GetVariadicResultsStart(CoroutineRuntimeContext* coroCtx)
{
    return coroCtx->m_variadicRetStart;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetVariadicResultsStart", DeegenSnippet_GetVariadicResultsStart)

