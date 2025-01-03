#include "define_deegen_common_snippet.h"
#include "drt/jit_inline_cache_utils.h"

static void* DeegenSnippet_CreateNewJitGenericIcForDfgJit(JitGenericInlineCacheSite* site, uint16_t dfgOpOrd, uint16_t icOrdInOp)
{
    return site->InsertForDfgJIT(dfgOpOrd, icOrdInOp);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitGenericICForDfgJit", DeegenSnippet_CreateNewJitGenericIcForDfgJit)
