#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_BoxFunctionObjectToTValue(FunctionObject* func)
{
    return TValue::Create<tFunction>(func);
}

DEFINE_DEEGEN_COMMON_SNIPPET("BoxFunctionObjectToTValue", DeegenSnippet_BoxFunctionObjectToTValue)
