#include "define_deegen_common_snippet.h"
#include "drt/jit_inline_cache_utils.h"

static void* DeegenSnippet_CreateNewJitGenericIcForBaselineJit(JitGenericInlineCacheSite* site, uint16_t traitKind)
{
    return site->InsertForBaselineJIT(traitKind);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitGenericICForBaselineJit", DeegenSnippet_CreateNewJitGenericIcForBaselineJit)
