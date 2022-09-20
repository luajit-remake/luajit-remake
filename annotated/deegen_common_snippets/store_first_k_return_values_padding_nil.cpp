#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

// Use uint64_t instead of TValue because in hand-generated IR we generally do not properly generate TBAA info for stack access
//
static void DeegenSnippet_StoreFirstKReturnValuesPaddingNil(uint64_t* retStart, uint64_t numRet, uint64_t* dst, uint64_t numToCopy)
{
    uint64_t len = std::min(numToCopy, numRet);
    memmove(dst, retStart, len * sizeof(uint64_t));
    if (numToCopy > numRet)
    {
        uint64_t fillVal = TValue::Nil().m_value;

        // Don't unroll this loop and generate a huge amount of code..
        //
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
        while (numRet < numToCopy)
        {
            dst[numRet] = fillVal;
            numRet++;
        }
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("StoreFirstKReturnValuesPaddingNil", DeegenSnippet_StoreFirstKReturnValuesPaddingNil)

