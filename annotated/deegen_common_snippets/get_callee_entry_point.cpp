#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static std::pair<HeapPtr<ExecutableCode>, void*> DeegenSnippet_GetCalleeEntryPoint(uint64_t target)
{
    HeapPtr<FunctionObject> o = reinterpret_cast<HeapPtr<FunctionObject>>(target);
    HeapPtr<ExecutableCode> ec = TCGet(o->m_executable).As();
    void* entrypoint = ec->m_bestEntryPoint;
    return std::make_pair(ec, entrypoint);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCalleeEntryPoint", DeegenSnippet_GetCalleeEntryPoint)
