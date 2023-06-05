#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static int64_t DeegenSnippet_GetInterpreterTierUpCounterFromCbHeapPtr(HeapPtr<CodeBlock> cb)
{
    return cb->m_interpreterTierUpCounter;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetInterpreterTierUpCounterFromCbHeapPtr", DeegenSnippet_GetInterpreterTierUpCounterFromCbHeapPtr)
