#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_MoveVariadicResultsForPrepend(uint64_t* stackBase, CoroutineRuntimeContext* coroCtx, uint64_t numPrepends, uint64_t numLocals, uint64_t limSlot)
{
    uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);
    uint64_t* dst = stackBase + limSlot;
    if (src >= dst)
    {
        // We already have enough space to prepend the values, no need to move
        //
        coroCtx->m_variadicRetStart -= numPrepends;
        coroCtx->m_numVariadicRets += numPrepends;
        return coroCtx->m_variadicRetStart;
    }

    uint32_t numResults = coroCtx->m_numVariadicRets;

    // Move src[0:numResults) to dst[0:numResults)
    // Since src < dst, we can always use a right-to-left move. Also, it's fine to overcopy by one to make things simpler.
    //
    {
        uint64_t elementsToMove = (numResults + 1) / 2 * 2;
        uint64_t* fromPtr = src + elementsToMove;
        uint64_t* toPtr = dst + elementsToMove;
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
        while (fromPtr > src)
        {
            fromPtr -= 2;
            toPtr -= 2;
            __builtin_memcpy_inline(toPtr, fromPtr, sizeof(TValue) * 2);
        }
    }

    coroCtx->m_variadicRetStart = reinterpret_cast<TValue*>(stackBase + numLocals);
    coroCtx->m_numVariadicRets = static_cast<uint32_t>(numResults + numPrepends);
    return stackBase + numLocals;
}

DEFINE_DEEGEN_COMMON_SNIPPET("MoveVariadicResultsForPrepend", DeegenSnippet_MoveVariadicResultsForPrepend)

