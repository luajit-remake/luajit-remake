#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_UpdateInterpreterTierUpCounterForReturnOrThrow(CodeBlock* cb, uint8_t* curBytecode)
{
    ssize_t diff = curBytecode - cb->GetBytecodeStream();
    // We want to add a fixed cost per function return to avoid corner cases
    // Currently bytecode stream is always a trailing array after codeblock,
    // so we simply add sizeof(CodeBlock) which also makes the computation simpler.
    //
    diff += sizeof(CodeBlock);
    cb->m_interpreterTierUpCounter -= diff;
}

DEFINE_DEEGEN_COMMON_SNIPPET("UpdateInterpreterTierUpCounterForReturnOrThrow", DeegenSnippet_UpdateInterpreterTierUpCounterForReturnOrThrow)
