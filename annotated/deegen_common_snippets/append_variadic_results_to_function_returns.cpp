#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_AppendVariadicResultsToFunctionReturns(uint64_t* retStart, uint64_t numRet, CoroutineRuntimeContext* coroCtx)
{
    uint32_t num = coroCtx->m_numVariadicRets;
    uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);
    uint64_t* dst = retStart + numRet;

    size_t i = 0;
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
    do
    {
        __builtin_memcpy_inline(dst + i, src + i, sizeof(TValue) * 2);
        i += 2;
    }
    while (i < num);

    return numRet + num;
}

DEFINE_DEEGEN_COMMON_SNIPPET("AppendVariadicResultsToFunctionReturns", DeegenSnippet_AppendVariadicResultsToFunctionReturns)

