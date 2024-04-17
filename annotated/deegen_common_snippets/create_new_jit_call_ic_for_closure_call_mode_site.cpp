#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_CreateNewJitCallIcForClosureCallModeSite(JitCallInlineCacheSite* site, uint16_t dcIcTraitKind, FunctionObject* func)
{
    return site->InsertInClosureCallMode(dcIcTraitKind, func);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitCallIcForClosureCallModeSite", DeegenSnippet_CreateNewJitCallIcForClosureCallModeSite)
