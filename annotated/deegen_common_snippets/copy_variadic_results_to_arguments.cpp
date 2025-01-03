#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_CopyVariadicResultsToArguments(uint64_t* dst, CoroutineRuntimeContext* coroCtx)
{
    uint32_t num = coroCtx->m_numVariadicRets;

    uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);
    memmove(dst, src, sizeof(uint64_t) * num);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CopyVariadicResultsToArguments", DeegenSnippet_CopyVariadicResultsToArguments)

