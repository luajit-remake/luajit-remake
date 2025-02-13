#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_SetUpVariadicResultInfo(CoroutineRuntimeContext* coroCtx, TValue* stackBase, DfgCodeBlock* codeBlock, uint64_t numResults)
{
    TValue* varResStart = stackBase + codeBlock->m_stackFrameNumSlots;
    coroCtx->m_variadicRetStart = varResStart;
    coroCtx->m_numVariadicRets = SafeIntegerCast<uint32_t>(numResults);
    return varResStart;
}

DEFINE_DEEGEN_COMMON_SNIPPET("SetUpVariadicResultInfo", DeegenSnippet_SetUpVariadicResultInfo)

