#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_CopyVariadicResultsToArgumentsForwardMayOvercopy(uint64_t* dst, CoroutineRuntimeContext* coroCtx)
{
    uint32_t num = coroCtx->m_numVariadicRets;

    uint64_t* src = reinterpret_cast<uint64_t*>(coroCtx->m_variadicRetStart);

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
        __builtin_memcpy_inline(dst + i, src + i, sizeof(TValue) * 2);
        i += 2;
    }
    while (i < num);    // TODO: investigate if we should add a unlikely here
}

DEFINE_DEEGEN_COMMON_SNIPPET("CopyVariadicResultsToArgumentsForwardMayOvercopy", DeegenSnippet_CopyVariadicResultsToArgumentsForwardMayOvercopy)

