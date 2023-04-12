#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static BaselineCodeBlock* DeegenSnippet_GetBaselineJitCodeBlockFromCodeBlock(CodeBlock* cb)
{
    return cb->m_baselineCodeBlock;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBaselineJitCodeBlockFromCodeBlock", DeegenSnippet_GetBaselineJitCodeBlockFromCodeBlock)
