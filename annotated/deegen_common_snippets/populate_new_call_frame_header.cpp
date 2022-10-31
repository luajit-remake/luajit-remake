#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PopulateNewCallFrameHeader(void* newStackBase, void* oldStackBase, CodeBlock* callerCb, uint8_t* curBytecode, uint64_t target, void* onReturn)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(newStackBase);
    hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
    hdr->m_caller = oldStackBase;
    hdr->m_retAddr = onReturn;
    hdr->m_callerBytecodeOffset = static_cast<uint32_t>(curBytecode - callerCb->m_bytecode);
    hdr->m_numVariadicArguments = 0;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNewCallFrameHeader", DeegenSnippet_PopulateNewCallFrameHeader)
