#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_CopyVariadicResultsToArgumentsForwardMayOvercopy(uint64_t* dst, uint64_t* stackBase, CoroutineRuntimeContext* coroCtx)
{
    int32_t srcOffset = coroCtx->m_variadicRetSlotBegin;
    uint32_t num = coroCtx->m_numVariadicRets;

    uint64_t* src = stackBase + srcOffset;

    // What we need is just a memmove. We hand-implement it because:
    // 1. Calling 'memmove' will result in a ton of code, which is bad for our case.
    //    In all sane cases, we are only copying a few elements at most.
    // 2. Often we know the copy can always be done forwardly (left-to-right), in which
    //    case we can save the direction branch (and a bunch of logic) in memmove.
    // 3. We don't need to precisely copy 'num' elements: copying a few more is
    //    completely fine. So we don't need the "tail" handling logic in memmove.
    //
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
}

DEFINE_DEEGEN_COMMON_SNIPPET("CopyVariadicResultsToArgumentsForwardMayOvercopy", DeegenSnippet_CopyVariadicResultsToArgumentsForwardMayOvercopy)

