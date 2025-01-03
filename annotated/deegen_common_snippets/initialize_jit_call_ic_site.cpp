#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_InitializeJitCallIcSite(JitCallInlineCacheSite* site)
{
    ConstructInPlace(site);
}

DEFINE_DEEGEN_COMMON_SNIPPET("InitializeJitCallIcSite", DeegenSnippet_InitializeJitCallIcSite)
