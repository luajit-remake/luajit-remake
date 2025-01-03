#include "define_deegen_common_snippet.h"

static void* DeegenSnippet_RoundPtrUpToMultipleOf(void* ptr, uint64_t mult)
{
    uint64_t val = reinterpret_cast<uint64_t>(ptr);
    val = (val + mult - 1) / mult * mult;
    return reinterpret_cast<void*>(val);
}

DEFINE_DEEGEN_COMMON_SNIPPET("RoundPtrUpToMultipleOf", DeegenSnippet_RoundPtrUpToMultipleOf)

