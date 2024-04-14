#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetUpvalue(uint64_t* stackBase, size_t upvalueOrd)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    return FunctionObject::GetUpvalueValue(TranslateToHeapPtr(hdr->m_func), upvalueOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetUpvalue", DeegenSnippet_GetUpvalue)

