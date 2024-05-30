#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetCodeBlockFromStackBase(void* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    ExecutableCode* callerEc = TranslateToRawPointer(hdr->m_func->m_executable.As());
    return callerEc;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCodeBlockFromStackBase", DeegenSnippet_GetCodeBlockFromStackBase)
