#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static BaselineCodeBlock* DeegenSnippet_GetBaselineJitCodeBlockFromCodeBlockHeapPtr(HeapPtr<CodeBlock> cb)
{
    return cb->m_baselineCodeBlock;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBaselineJitCodeBlockFromCodeBlockHeapPtr", DeegenSnippet_GetBaselineJitCodeBlockFromCodeBlockHeapPtr)
