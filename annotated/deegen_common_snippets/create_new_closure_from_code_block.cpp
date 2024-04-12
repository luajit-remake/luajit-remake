#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static FunctionObject* DeegenSnippet_CreateNewClosureFromCodeBlock(CodeBlock* codeblockOfClosureToCreate, CoroutineRuntimeContext* coroCtx, uint64_t* stackBase, size_t selfStackFrameOrdinal)
{
    return TranslateToRawPointer(FunctionObject::CreateAndFillUpvalues(codeblockOfClosureToCreate, coroCtx, reinterpret_cast<TValue*>(stackBase), TranslateToRawPointer(StackFrameHeader::Get(stackBase)->m_func), selfStackFrameOrdinal).As());
}

DEFINE_DEEGEN_COMMON_SNIPPET("CreateNewClosureFromCodeBlock", DeegenSnippet_CreateNewClosureFromCodeBlock)
