#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_PopulateNewCallFrameHeader(StackFrameHeader* newStackframeBase, void* oldStackFrameBase, CodeBlock* callerCb, uint8_t* curBytecode, uint64_t target, void* onReturn, bool doNotFillFunc)
{
    StackFrameHeader* hdr = newStackframeBase - 1;
    if (!doNotFillFunc)
    {
        hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
    }
    hdr->m_retAddr = onReturn;
    hdr->m_caller = oldStackFrameBase;
    hdr->m_callerBytecodeOffset = static_cast<uint32_t>(curBytecode - callerCb->m_bytecode);
    hdr->m_numVariadicArguments = 0;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNewCallFrameHeader", DeegenSnippet_PopulateNewCallFrameHeader)
