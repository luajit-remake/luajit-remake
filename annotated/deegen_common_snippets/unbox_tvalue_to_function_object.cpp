#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static FunctionObject* DeegenSnippet_BoxFunctionObjectToTValue(TValue tv)
{
    return tv.As<tFunction>();
}

DEFINE_DEEGEN_COMMON_SNIPPET("UnboxTValueToFunctionObject", DeegenSnippet_BoxFunctionObjectToTValue)
