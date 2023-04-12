#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetBytecodePtrFromBytecodeOffset(CodeBlock* cb, uint64_t offset)
{
    return reinterpret_cast<uint8_t*>(cb->m_bytecodeStream) + offset;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrFromBytecodeOffset", DeegenSnippet_GetBytecodePtrFromBytecodeOffset)

