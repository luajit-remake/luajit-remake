#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "drt/baseline_jit_codegen_helper.h"

static void DeegenSnippet_InitializeJitGenericIcSite(JitGenericInlineCacheSite* site)
{
    ConstructInPlace(site);
}

DEFINE_DEEGEN_COMMON_SNIPPET("InitializeJitGenericIcSite", DeegenSnippet_InitializeJitGenericIcSite)
