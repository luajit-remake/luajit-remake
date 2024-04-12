#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static ExecutableCode* DeegenSnippet_GetCbFromTValueFuncObj(TValue tv)
{
    HeapPtr<FunctionObject> o = tv.As<tFunction>();
    HeapPtr<ExecutableCode> ec = TCGet(o->m_executable).As();
    return TranslateToRawPointer(VM_GetActiveVMForCurrentThread(), ec);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCbFromTValueFuncObj", DeegenSnippet_GetCbFromTValueFuncObj)
