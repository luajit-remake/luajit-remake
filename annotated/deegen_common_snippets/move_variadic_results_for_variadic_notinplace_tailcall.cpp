#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// Return true if the variadic results are moved
//
static bool DeegenSnippet_MoveVariadicResultsForVariadicNotInPlaceTailCall(uint64_t* stackBase, CoroutineRuntimeContext* coroCtx, uint64_t numArgs)
{
    uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);
    uint64_t* dst = stackBase + numArgs;

    if (src < stackBase || src >= dst)
    {
        return false;
    }

    memmove(dst, src, sizeof(uint64_t) * coroCtx->m_numVariadicRets);
    return true;
}

DEFINE_DEEGEN_COMMON_SNIPPET("MoveVariadicResultsForVariadicNotInPlaceTailCall", DeegenSnippet_MoveVariadicResultsForVariadicNotInPlaceTailCall)

