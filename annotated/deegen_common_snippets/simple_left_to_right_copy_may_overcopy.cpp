#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_SimpleLeftToRightCopyMayOvercopy(uint64_t* dst, uint64_t* src, uint64_t num)
{
    // We have a common but weird use case: we want to do a memmove, but
    // 1. We know a left-to-right copy is correct, and it's fine to over-copy a few elements
    // 2. We are usually only copying a few elements
    // In such case doing a memmove() call seems bad since it invalidates registers, runs a lot of
    // unnecessary code and branches, and doesn't really give the benefit of using memmove.
    // So we hand implement it for our use case with a loop that cannot be unrolled.
    //
    // But this introduces another issue. Sometimes 'num' is a constant, and in that case we want to
    // let LLVM unroll the loop and generate the most suitable implementation.
    //
    // So we have the following '__builtin_constant_p' hack...
    //
    if (!__builtin_constant_p(num))
    {
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
    else
    {
        memmove(dst, src, num * sizeof(uint64_t));
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("SimpleLeftToRightCopyMayOvercopy", DeegenSnippet_SimpleLeftToRightCopyMayOvercopy)

// Do not run optimization, extract directly, so that '__builtin_constant_p' is not prematurely lowered
//
DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
