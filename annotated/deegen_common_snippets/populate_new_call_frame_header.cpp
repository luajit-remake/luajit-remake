#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PopulateNewCallFrameHeader(void* newStackBase, void* oldStackBase, uint32_t curBytecodeSysHeapPtr, FunctionObject* target, void* onReturn)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(newStackBase);
    hdr->m_func = target;
    hdr->m_caller = oldStackBase;
    hdr->m_retAddr = onReturn;
    // We just want to do
    //     hdr->m_callerBytecodePtr = curBytecodeSysHeapPtr;
    //     hdr->m_numVariadicArguments = 0;
    // This could have been accomplished by one uint64_t write, but the compiler won't optimize it for whatever reason.
    // So we just do the ugly hack by ourselves here. This relies on little-endianness.
    //
    uint32_t u32 = curBytecodeSysHeapPtr;
    uint64_t asU64 = static_cast<uint64_t>(u32);
    *reinterpret_cast<uint64_t*>(&hdr->m_callerBytecodePtr) = asU64;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNewCallFrameHeader", DeegenSnippet_PopulateNewCallFrameHeader)
