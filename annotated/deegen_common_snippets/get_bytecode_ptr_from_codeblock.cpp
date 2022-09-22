#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static uint8_t* DeegenSnippet_GetBytecodePtrFromCodeBlock(CodeBlock* cb)
{
    return cb->m_bytecode;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrFromCodeBlock", DeegenSnippet_GetBytecodePtrFromCodeBlock)

