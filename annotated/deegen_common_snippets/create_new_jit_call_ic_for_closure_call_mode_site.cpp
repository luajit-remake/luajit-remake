#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_CreateNewJitCallIcForClosureCallModeSite(JitCallInlineCacheSite* site, uint16_t dcIcTraitKind, TValue tv)
{
    return site->InsertInClosureCallMode(dcIcTraitKind, tv);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitCallIcForClosureCallModeSite", DeegenSnippet_CreateNewJitCallIcForClosureCallModeSite)
