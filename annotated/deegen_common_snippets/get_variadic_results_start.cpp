#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t* DeegenSnippet_GetVariadicResultsStart(uint64_t* stackBase, CoroutineRuntimeContext* coroCtx)
{
    int32_t offset = coroCtx->m_variadicRetSlotBegin;
    return stackBase + offset;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetVariadicResultsStart", DeegenSnippet_GetVariadicResultsStart)

