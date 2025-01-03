#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetBytecodePtrFromCodeBlock(CodeBlock* cb)
{
    return cb->m_bytecodeStream;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrFromCodeBlock", DeegenSnippet_GetBytecodePtrFromCodeBlock)

