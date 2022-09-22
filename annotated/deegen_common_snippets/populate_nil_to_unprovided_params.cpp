#include "force_release_build.h"

#include "define_deegen_common_snippet.h"

static void DeegenSnippet_PopulateNilToUnprovidedParams(uint64_t* paramStart, uint64_t numProvidedParams, uint64_t numNeededParams, uint64_t nilValue)
{
    if (!__builtin_constant_p(numNeededParams))
    {
        // Don't unroll and generate a big pile of code
        //
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
        while (numProvidedParams < numNeededParams)
        {
            paramStart[numProvidedParams] = nilValue;
            numProvidedParams++;
        }
    }
    else
    {
        // 'numNeededParams' is a constant which means the user intends us to unroll the loop and let LLVM optimize it
        // We also take advantage of the fact that we can write a few more values without problem, to reduce the # of branches
        //
        while (numProvidedParams < numNeededParams)
        {
            paramStart[numProvidedParams] = nilValue;
            paramStart[numProvidedParams + 1] = nilValue;
            numProvidedParams += 2;
        }
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNilToUnprovidedParams", DeegenSnippet_PopulateNilToUnprovidedParams)

// Do not run optimization, extract directly, so that '__builtin_constant_p' is not prematurely lowered
//
DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
