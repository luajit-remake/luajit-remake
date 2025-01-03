#include "define_deegen_common_snippet.h"

static uint64_t DeegenSnippet_I64SubSaturateToZeroImpl(int64_t val, int64_t valToSub)
{
    // Do subtraction in u64 to avoid overflow UB
    //
    return (val < valToSub) ? 0 : (static_cast<uint64_t>(val) - static_cast<uint64_t>(valToSub));
}

DEFINE_DEEGEN_COMMON_SNIPPET("I64SubSaturateToZeroImpl", DeegenSnippet_I64SubSaturateToZeroImpl)

