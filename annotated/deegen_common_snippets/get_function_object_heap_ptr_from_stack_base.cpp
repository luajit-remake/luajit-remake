#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static HeapPtr<FunctionObject> DeegenSnippet_GetFunctionObjectHeapPtrFromStackBase(uint64_t* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    return hdr->m_func;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetFunctionObjectHeapPtrFromStackBase", DeegenSnippet_GetFunctionObjectHeapPtrFromStackBase)

