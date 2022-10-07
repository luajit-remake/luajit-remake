#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// Return true if the variadic results are moved
//
static bool DeegenSnippet_MoveVariadicResultsForVariadicNotInPlaceTailCall(uint64_t* stackBase, CoroutineRuntimeContext* coroCtx, uint64_t numArgs)
{
    int32_t srcOffset = coroCtx->m_variadicRetSlotBegin;
    if (srcOffset < 0)
    {
        return false;
    }

    if (numArgs <= static_cast<uint32_t>(srcOffset))
    {
        return false;
    }

    uint64_t* src = stackBase + srcOffset;
    uint64_t* dst = stackBase + numArgs;
    memmove(dst, src, sizeof(uint64_t) * coroCtx->m_numVariadicRets);
    return true;
}

DEFINE_DEEGEN_COMMON_SNIPPET("MoveVariadicResultsForVariadicNotInPlaceTailCall", DeegenSnippet_MoveVariadicResultsForVariadicNotInPlaceTailCall)

