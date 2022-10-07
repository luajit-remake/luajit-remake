#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetCodeBlockFromStackBase(void* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
    return TranslateToRawPointer(callerEc);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCodeBlockFromStackBase", DeegenSnippet_GetCodeBlockFromStackBase)
