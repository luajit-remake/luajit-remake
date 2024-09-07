#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void* DeegenSnippet_GetBaselineCodeBlockFromStackBase(void* stackBase)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    ExecutableCode* callerEc = hdr->m_func->m_executable.As();
    CodeBlock* callerCb = static_cast<CodeBlock*>(callerEc);
    return callerCb->m_baselineCodeBlock;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetBaselineCodeBlockFromStackBase", DeegenSnippet_GetBaselineCodeBlockFromStackBase)
