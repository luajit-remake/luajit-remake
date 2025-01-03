#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static HeapPtr<ExecutableCode> DeegenSnippet_GetCbHeapPtrFromTValueFuncObj(TValue tv)
{
    HeapPtr<FunctionObject> o = tv.As<tFunction>();
    HeapPtr<ExecutableCode> ec = TCGet(o->m_executable).As();
    return ec;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCbHeapPtrFromTValueFuncObj", DeegenSnippet_GetCbHeapPtrFromTValueFuncObj)
