#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static uint64_t* DeegenSnippet_GetVariadicArgStart(uint64_t* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    uint32_t num = hdr->m_numVariadicArguments;
    return reinterpret_cast<uint64_t*>(hdr) - num;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetVariadicArgStart", DeegenSnippet_GetVariadicArgStart)

