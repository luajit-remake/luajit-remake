#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_BoxFunctionObjectToTValue(uint64_t func64)
{
    return TValue::Create<tFunction>(OffsetToPtr<FunctionObject>(func64));
}

DEFINE_DEEGEN_COMMON_SNIPPET("BoxFunctionObjectToTValue", DeegenSnippet_BoxFunctionObjectToTValue)
