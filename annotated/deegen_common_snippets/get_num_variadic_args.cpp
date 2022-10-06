#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static uint64_t DeegenSnippet_GetNumVariadicArgs(uint64_t* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::GetStackFrameHeader(stackBase);
    return hdr->m_numVariadicArguments;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetNumVariadicArgs", DeegenSnippet_GetNumVariadicArgs)

