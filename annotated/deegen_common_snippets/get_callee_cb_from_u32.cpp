#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static CodeBlock* DeegenSnippet_GetCalleeCbFromU32(uint32_t u32)
{
    return TranslateToRawPointer(HeapPtr<CodeBlock>(static_cast<uintptr_t>(u32)));
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCalleeCbFromU32", DeegenSnippet_GetCalleeCbFromU32)
