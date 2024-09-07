#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static ExecutableCode* DeegenSnippet_GetCbFromTValueFuncObj(TValue tv)
{
    FunctionObject* o = tv.As<tFunction>();
    ExecutableCode* ec = o->m_executable.As();
    return ec;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCbFromTValueFuncObj", DeegenSnippet_GetCbFromTValueFuncObj)
