#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint8_t* DeegenSnippet_GetBytecodePtrAfterReturnFromCall(void* calleeStackBase)
{
    StackFrameHeader* calleeHdr = StackFrameHeader::Get(calleeStackBase);
    return TranslateToRawPointer(calleeHdr->m_callerBytecodePtr.As());
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrAfterReturnFromCall", DeegenSnippet_GetBytecodePtrAfterReturnFromCall)
