#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static void DeegenSnippet_PopulateNewCallFrameHeaderForCallFromCFunc(void* newStackBase, void* oldStackBase, void* onReturn)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(newStackBase);
    hdr->m_retAddr = onReturn;
    hdr->m_caller = oldStackBase;
    hdr->m_callerBytecodeOffset = 0;
    hdr->m_numVariadicArguments = 0;
}

DEFINE_DEEGEN_COMMON_SNIPPET("PopulateNewCallFrameHeaderForCallFromCFunc", DeegenSnippet_PopulateNewCallFrameHeaderForCallFromCFunc)
