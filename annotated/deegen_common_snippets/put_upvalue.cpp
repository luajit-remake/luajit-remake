#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_PutUpvalue(uint64_t* stackBase, size_t upvalueOrd, TValue valueToPut)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    Assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    HeapPtr<Upvalue> uv = FunctionObject::GetMutableUpvaluePtr(hdr->m_func, upvalueOrd);
    TValue* ptr = uv->m_ptr;
    *ptr = valueToPut;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PutUpvalue", DeegenSnippet_PutUpvalue)

