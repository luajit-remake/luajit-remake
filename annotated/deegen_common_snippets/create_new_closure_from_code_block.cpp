#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "bytecode.h"

static HeapPtr<FunctionObject> DeegenSnippet_CreateNewClosureFromCodeBlock(CodeBlock* codeblockOfClosureToCreate, CoroutineRuntimeContext* coroCtx, uint64_t* stackFrameBase)
{
    return FunctionObject::CreateAndFillUpvalues(codeblockOfClosureToCreate, coroCtx, reinterpret_cast<TValue*>(stackFrameBase), StackFrameHeader::GetStackFrameHeader(stackFrameBase)->m_func).As();
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewClosureFromCodeBlock", DeegenSnippet_CreateNewClosureFromCodeBlock)

