#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_StoreReturnValuesAsVariadicResults(CoroutineRuntimeContext* coroCtx, TValue* retStart, uint64_t numRet)
{
    coroCtx->m_variadicRetStart = retStart;
    coroCtx->m_numVariadicRets = static_cast<uint32_t>(numRet);
}

DEFINE_DEEGEN_COMMON_SNIPPET("StoreReturnValuesAsVariadicResults", DeegenSnippet_StoreReturnValuesAsVariadicResults)

