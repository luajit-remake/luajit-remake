#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetKthVariadicResult(CoroutineRuntimeContext* coroCtx, uint64_t ord)
{
    TValue* start = coroCtx->m_variadicRetStart;
    return (ord < coroCtx->m_numVariadicRets) ? start[ord] : TValue::Create<tNil>();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetKthVariadicResult", DeegenSnippet_GetKthVariadicResult)

