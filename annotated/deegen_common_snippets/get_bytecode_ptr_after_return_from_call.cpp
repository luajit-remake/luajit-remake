#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static uint8_t* DeegenSnippet_GetBytecodePtrAfterReturnFromCall(void* stackframeBase, CodeBlock* cb)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackframeBase);
    uint32_t bytecodeOffset = hdr->m_callerBytecodeOffset;
    uint8_t* bytecodeBase = cb->m_bytecode;
    return bytecodeBase + bytecodeOffset;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBytecodePtrAfterReturnFromCall", DeegenSnippet_GetBytecodePtrAfterReturnFromCall)
