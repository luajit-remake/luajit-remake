#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "drt/baseline_jit_codegen_helper.h"

static void* DeegenSnippet_CreateNewJitGenericIc(JitGenericInlineCacheSite* site, uint16_t traitKind)
{
    return site->Insert(traitKind);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitGenericIC", DeegenSnippet_CreateNewJitGenericIc)
