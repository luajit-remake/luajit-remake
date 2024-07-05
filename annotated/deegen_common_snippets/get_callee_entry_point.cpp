#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static std::pair<ExecutableCode*, void*> DeegenSnippet_GetCalleeEntryPoint(TValue tv)
{
    ExecutableCode* ec = tv.As<tFunction>()->m_executable.As();
    void* entrypoint = ec->m_bestEntryPoint;
    return std::make_pair(ec, entrypoint);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCalleeEntryPoint", DeegenSnippet_GetCalleeEntryPoint)
