#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t DeegenSnippet_GetFuncObjAsU64FromTValue(TValue tv)
{
    HeapPtr<FunctionObject> o = tv.As<tFunction>();
    return reinterpret_cast<uint64_t>(o);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetFuncObjAsU64FromTValue", DeegenSnippet_GetFuncObjAsU64FromTValue)
