#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_PutUpvalue(uint64_t* stackBase, size_t upvalueOrd, TValue valueToPut)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackBase);
    assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    TValue* ptr = TCGet(hdr->m_func->m_upvalues[upvalueOrd]).As()->m_ptr;
    *ptr = valueToPut;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PutUpvalue", DeegenSnippet_PutUpvalue)

