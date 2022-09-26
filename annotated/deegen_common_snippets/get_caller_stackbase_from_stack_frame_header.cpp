#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static void* DeegenSnippet_GetCallerStackBaseFromStackFrameHeader(StackFrameHeader* hdr)
{
    return hdr->m_caller;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetCallerStackBaseFromStackFrameHeader", DeegenSnippet_GetCallerStackBaseFromStackFrameHeader)

