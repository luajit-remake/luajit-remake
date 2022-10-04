#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_CopyVariadicResultsToArguments(uint64_t* dst, uint64_t* stackBase, CoroutineRuntimeContext* coroCtx)
{
    int32_t srcOffset = coroCtx->m_variadicRetSlotBegin;
    uint32_t num = coroCtx->m_numVariadicRets;

    uint64_t* src = stackBase + srcOffset;
    memmove(dst, src, sizeof(uint64_t) * num);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CopyVariadicResultsToArguments", DeegenSnippet_CopyVariadicResultsToArguments)

