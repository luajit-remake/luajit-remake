#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t* DeegenSnippet_GetEndOfCallFrameFromInterpreterCodeBlock(uint64_t* stackBase, CodeBlock* cb)
{
    return stackBase + cb->m_stackFrameNumSlots;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetEndOfCallFrameFromInterpreterCodeBlock", DeegenSnippet_GetEndOfCallFrameFromInterpreterCodeBlock)

