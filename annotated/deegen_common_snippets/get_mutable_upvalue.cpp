#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetMutableUpvalueValue(uint64_t* stackBase, size_t upvalueOrd)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    Assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    HeapPtr<Upvalue> uvPtr = FunctionObject::GetMutableUpvaluePtr(hdr->m_func, upvalueOrd);
    return *(uvPtr->m_ptr);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetMutableUpvalueValue", DeegenSnippet_GetMutableUpvalueValue)

