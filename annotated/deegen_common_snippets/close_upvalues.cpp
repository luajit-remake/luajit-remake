#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_CloseUpvalues(TValue* base, CoroutineRuntimeContext* coroCtx)
{
    coroCtx->CloseUpvalues(base);
}

DEFINE_DEEGEN_COMMON_SNIPPET("CloseUpvalues", DeegenSnippet_CloseUpvalues)

