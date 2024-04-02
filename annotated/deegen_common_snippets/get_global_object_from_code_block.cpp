#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TableObject* DeegenSnippet_GetGlobalObjectFromCodeBlock(CodeBlock* cb)
{
    return TranslateToRawPointer(cb->m_globalObject.As());
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetGlobalObjectFromCodeBlock", DeegenSnippet_GetGlobalObjectFromCodeBlock)

