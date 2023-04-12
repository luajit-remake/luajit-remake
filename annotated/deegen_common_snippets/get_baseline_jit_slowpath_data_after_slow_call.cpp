#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetBaselineJitSlowpathDataAfterSlowCall(void* calleeStackBase, CodeBlock* cb)
{
    StackFrameHeader* calleeHdr = StackFrameHeader::Get(calleeStackBase);
    uint32_t value = calleeHdr->m_callerBytecodePtr.m_value;
    uint64_t bcb = reinterpret_cast<uint64_t>(cb->m_baselineCodeBlock);
    uint32_t offset = value - static_cast<uint32_t>(bcb);
    return reinterpret_cast<void*>(static_cast<uint64_t>(offset) + bcb);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBaselineJitSlowpathDataAfterSlowCall", DeegenSnippet_GetBaselineJitSlowpathDataAfterSlowCall)
