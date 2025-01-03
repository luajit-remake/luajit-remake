#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetKthVariadicArg(uint64_t* stackBase, uint64_t argOrd)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    uint32_t num = hdr->m_numVariadicArguments;
    TValue* argStart = reinterpret_cast<TValue*>(hdr) - num;
    return (argOrd < num) ? argStart[argOrd] : TValue::Create<tNil>();
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetKthVariadicArg", DeegenSnippet_GetKthVariadicArg)

