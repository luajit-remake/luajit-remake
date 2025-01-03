#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_GetStackRegSpillRegionOffsetFromDfgCodeBlock(DfgCodeBlock* cb)
{
    return cb->GetStackRegSpillRegionOffset();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetStackRegSpillRegionOffsetFromDfgCodeBlock", DeegenSnippet_GetStackRegSpillRegionOffsetFromDfgCodeBlock)

