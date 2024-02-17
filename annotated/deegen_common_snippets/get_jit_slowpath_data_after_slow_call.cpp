#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// 'bcb' may be a BaselineCodeBlock or a DfgCodeBlock, since the computation rule is the same
//
static void* DeegenSnippet_GetJitSlowpathDataAfterSlowCall(void* calleeStackBase, void* bcb)
{
    StackFrameHeader* calleeHdr = StackFrameHeader::Get(calleeStackBase);
    uint32_t value = calleeHdr->m_callerBytecodePtr.m_value;
    uint64_t bcbU64 = reinterpret_cast<uint64_t>(bcb);
    uint32_t offset = value - static_cast<uint32_t>(bcbU64);
    return reinterpret_cast<void*>(static_cast<uint64_t>(offset) + bcbU64);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetJitSlowpathDataAfterSlowCall", DeegenSnippet_GetJitSlowpathDataAfterSlowCall)
