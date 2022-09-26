#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static void* DeegenSnippet_GetRetAddrFromStackFrameHeader(StackFrameHeader* hdr)
{
    return hdr->m_retAddr;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetRetAddrFromStackFrameHeader", DeegenSnippet_GetRetAddrFromStackFrameHeader)
