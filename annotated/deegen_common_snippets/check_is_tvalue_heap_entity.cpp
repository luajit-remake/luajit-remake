#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static bool DeegenSnippet_IsTValueHeapEntity(TValue tv)
{
    return tv.Is<tHeapEntity>();
}

DEFINE_DEEGEN_COMMON_SNIPPET("IsTValueHeapEntity", DeegenSnippet_IsTValueHeapEntity)
