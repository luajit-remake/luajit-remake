#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "tvalue.h"

static void* DeegenSnippet_GetRetAddrFromStackBase(void* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackBase);
    return hdr->m_retAddr;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetRetAddrFromStackBase", DeegenSnippet_GetRetAddrFromStackBase)
