#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static void* DeegenSnippet_GetCallerStackBaseFromStackBase(void* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackBase);
    return hdr->m_caller;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCallerStackBaseFromStackBase", DeegenSnippet_GetCallerStackBaseFromStackBase)

