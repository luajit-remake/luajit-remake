#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

// Returns the updated stack frame base
//
static void* DeegenSnippet_MoveCallFrameHeaderForTailCall(void* stackBase, uint64_t target)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    if (likely(hdr->m_numVariadicArguments == 0))
    {
        hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
        return stackBase;
    }
    else
    {
        StackFrameHeader* dstHdr = reinterpret_cast<StackFrameHeader*>(reinterpret_cast<uint64_t*>(hdr) - hdr->m_numVariadicArguments);
        *dstHdr = *hdr;
        dstHdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
        dstHdr->m_numVariadicArguments = 0;
        return dstHdr + 1;
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("MoveCallFrameHeaderForTailCall", DeegenSnippet_MoveCallFrameHeaderForTailCall)
