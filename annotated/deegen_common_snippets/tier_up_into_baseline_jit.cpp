#include "define_deegen_common_snippet.h"
#include "drt/baseline_jit_codegen_helper.h"

static BaselineCodeBlockAndEntryPoint DeegenSnippet_TierUpIntoBaselineJit(HeapPtr<CodeBlock> cb)
{
    return deegen_prepare_tier_up_into_baseline_jit(cb);
}

DEFINE_DEEGEN_COMMON_SNIPPET("TierUpIntoBaselineJit", DeegenSnippet_TierUpIntoBaselineJit)
