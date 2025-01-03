#include "define_deegen_common_snippet.h"
#include "drt/jit_inline_cache_utils.h"

static void DeegenSnippet_InitializeJitGenericIcSite(JitGenericInlineCacheSite* site)
{
    ConstructInPlace(site);
}

DEFINE_DEEGEN_COMMON_SNIPPET("InitializeJitGenericIcSite", DeegenSnippet_InitializeJitGenericIcSite)
