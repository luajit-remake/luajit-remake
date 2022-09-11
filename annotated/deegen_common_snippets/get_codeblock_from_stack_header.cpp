#include "define_deegen_common_snippet.h"
#include "bytecode.h"

void* DeegenSnippet_GetCodeBlockFromStackFrameBase(void* stackframeBase)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframeBase);
    HeapPtr<ExecutableCode> callerEc = TCGet(hdr->m_func->m_executable).As();
    return TranslateToRawPointer(callerEc);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCodeBlockFromStackFrameBase", DeegenSnippet_GetCodeBlockFromStackFrameBase)
