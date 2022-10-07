#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_AppendVariadicResultsToFunctionReturns(uint64_t* stackbase, uint64_t* retStart, uint64_t numRet, CoroutineRuntimeContext* coroCtx)
{
    int32_t srcOffset = coroCtx->m_variadicRetSlotBegin;
    uint32_t num = coroCtx->m_numVariadicRets;
    uint64_t* src = stackbase + srcOffset;
    uint64_t* dst = retStart + numRet;

    size_t i = 0;
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
    do
    {
        uint64_t tmp1 = src[0];
        uint64_t tmp2 = src[1];
        dst[0] = tmp1;
        dst[1] = tmp2;
        src += 2;
        dst += 2;
        i += 2;
    }
    while (i < num);

    return numRet + num;
}

DEFINE_DEEGEN_COMMON_SNIPPET("AppendVariadicResultsToFunctionReturns", DeegenSnippet_AppendVariadicResultsToFunctionReturns)

