#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_StoreVariadicArgsAsVariadicResults(uint64_t* stackBase, CoroutineRuntimeContext* coroCtx)
{
    StackFrameHeader* hdr = StackFrameHeader::Get(stackBase);
    uint32_t numVarArgs = hdr->m_numVariadicArguments;
    coroCtx->m_variadicRetSlotBegin = -static_cast<int32_t>(numVarArgs + x_numSlotsForStackFrameHeader);
    coroCtx->m_numVariadicRets = numVarArgs;
}

DEFINE_DEEGEN_COMMON_SNIPPET("StoreVariadicArgsAsVariadicResults", DeegenSnippet_StoreVariadicArgsAsVariadicResults)

