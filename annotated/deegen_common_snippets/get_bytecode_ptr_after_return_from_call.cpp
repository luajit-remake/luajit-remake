#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint8_t* DeegenSnippet_GetBytecodePtrAfterReturnFromCall(void* calleeStackBase, CodeBlock* cb)
{
    StackFrameHeader* calleeHdr = StackFrameHeader::GetStackFrameHeader(calleeStackBase);
    uint32_t bytecodeOffset = calleeHdr->m_callerBytecodeOffset;
    uint8_t* bytecodeBase = cb->m_bytecode;
    return bytecodeBase + bytecodeOffset;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrAfterReturnFromCall", DeegenSnippet_GetBytecodePtrAfterReturnFromCall)
