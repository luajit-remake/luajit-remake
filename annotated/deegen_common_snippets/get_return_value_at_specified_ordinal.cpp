#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

// Use uint64_t instead of TValue because in hand-generated IR we generally do not properly generate TBAA info for stack access
//
static uint64_t DeegenSnippet_GetReturnValueAtSpecifiedOrdinal(uint64_t* retStart, uint64_t numRet, uint64_t desiredOrd)
{
    if (desiredOrd < numRet)
    {
        return retStart[desiredOrd];
    }
    else
    {
        return TValue::Nil().m_value;
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetReturnValueAtSpecifiedOrdinal", DeegenSnippet_GetReturnValueAtSpecifiedOrdinal)

