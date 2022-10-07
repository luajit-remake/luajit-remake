#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static TValue DeegenSnippet_GetUpvalue(uint64_t* stackBase, size_t upvalueOrd)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackBase);
    assert(upvalueOrd < hdr->m_func->m_numUpvalues);
    TValue result = *TCGet(hdr->m_func->m_upvalues[upvalueOrd]).As()->m_ptr;
    return result;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetUpvalue", DeegenSnippet_GetUpvalue)

