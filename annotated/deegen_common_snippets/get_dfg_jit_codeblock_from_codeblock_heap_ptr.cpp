#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static DfgCodeBlock* DeegenSnippet_GetDfgJitCodeBlockFromCodeBlockHeapPtr(HeapPtr<CodeBlock> cb)
{
    return cb->m_dfgCodeBlock;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetDfgJitCodeBlockFromCodeBlockHeapPtr", DeegenSnippet_GetDfgJitCodeBlockFromCodeBlockHeapPtr)
