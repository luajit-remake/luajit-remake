#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_SetUpVariadicResultInfo(CoroutineRuntimeContext* coroCtx, TValue* stackBase, int64_t retStartSlot, uint64_t numRets)
{
    coroCtx->m_variadicRetStart = stackBase + retStartSlot;
    coroCtx->m_numVariadicRets = SafeIntegerCast<uint32_t>(numRets);
}

DEFINE_DEEGEN_COMMON_SNIPPET("SetUpVariadicResultInfo", DeegenSnippet_SetUpVariadicResultInfo)

