#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static std::pair<ExecutableCode*, void*> DeegenSnippet_GetCalleeEntryPoint(uint64_t target)
{
    FunctionObject* o = reinterpret_cast<FunctionObject*>(target);
    HeapPtr<ExecutableCode> ec = o->m_executable.As();
    void* entrypoint = ec->m_bestEntryPoint;
    return std::make_pair(TranslateToRawPointer(ec), entrypoint);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCalleeEntryPoint", DeegenSnippet_GetCalleeEntryPoint)
