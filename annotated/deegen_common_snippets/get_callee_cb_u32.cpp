#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint32_t DeegenSnippet_GetCalleeCbU32FromTValue(TValue tv)
{
    SystemHeapPointer<ExecutableCode> ec = tv.As<tFunction>()->m_executable;
    return ec.m_value;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCalleeCbU32FromTValue", DeegenSnippet_GetCalleeCbU32FromTValue)
