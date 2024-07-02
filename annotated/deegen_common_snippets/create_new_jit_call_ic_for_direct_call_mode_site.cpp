#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_CreateNewJitCallIcForDirectCallModeSite(JitCallInlineCacheSite* site, uint16_t dcIcTraitKind, TValue tv, uint8_t* transitedToCCMode)
{
    return site->InsertInDirectCallMode(dcIcTraitKind, TranslateToRawPointer(tv.As<tFunction>()), transitedToCCMode);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewJitCallIcForDirectCallModeSite", DeegenSnippet_CreateNewJitCallIcForDirectCallModeSite)
