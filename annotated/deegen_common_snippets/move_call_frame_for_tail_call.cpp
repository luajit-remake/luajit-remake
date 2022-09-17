#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

// Returns the updated stack frame base
//
static void* DeegenSnippet_MoveCallFrameForTailCall(StackFrameHeader* stackframeBase, uint64_t target, uint64_t numArgs)
{
    StackFrameHeader* hdr = stackframeBase - 1;
    if (likely(hdr->m_numVariadicArguments == 0))
    {
        hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
        return stackframeBase;
    }
    else
    {
        hdr->m_func = reinterpret_cast<HeapPtr<FunctionObject>>(target);
        hdr->m_numVariadicArguments = 0;

        StackFrameHeader* dstHdr = reinterpret_cast<StackFrameHeader*>(reinterpret_cast<uint64_t*>(hdr) - hdr->m_numVariadicArguments);

        size_t num = numArgs + sizeof(StackFrameHeader) / sizeof(uint64_t);
        uint64_t* src = reinterpret_cast<uint64_t*>(hdr);
        uint64_t* dst = reinterpret_cast<uint64_t*>(dstHdr);

        // Copied from SimpleLeftToRightCopyMayOvercopy, since we don't support calling another snippet from one snippet yet..
        //
        if (!__builtin_constant_p(num))
        {
            size_t i = 0;
#pragma clang loop unroll(disable)
#pragma clang loop vectorize(disable)
            do
            {
                uint64_t tmp1 = src[0];
                uint64_t tmp2 = src[1];
                dst[0] = tmp1;
                dst[1] = tmp2;
                src += 2;
                dst += 2;
                i += 2;
            }
            while (i < num);
        }
        else
        {
            memmove(dst, src, num * sizeof(uint64_t));
        }

        return dstHdr + 1;
    }
}

DEFINE_DEEGEN_COMMON_SNIPPET("MoveCallFrameForTailCall", DeegenSnippet_MoveCallFrameForTailCall)

// Same as in SimpleLeftToRightCopyMayOvercopy, do not run optimization, extract directly, so that '__builtin_constant_p' is not prematurely lowered
//
DEEGEN_COMMON_SNIPPET_OPTION_DO_NOT_OPTIMIZE_BEFORE_EXTRACT
