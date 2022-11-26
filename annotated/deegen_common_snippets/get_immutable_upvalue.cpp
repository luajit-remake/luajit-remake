#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetImmutableUpvalueValue(uint64_t* stackBase, size_t upvalueOrd)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    return FunctionObject::GetImmutableUpvalueValue(hdr->m_func, upvalueOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetImmutableUpvalueValue", DeegenSnippet_GetImmutableUpvalueValue)

