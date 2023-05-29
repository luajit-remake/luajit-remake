#include "force_release_build.h"

#include "define_deegen_common_snippet.h"
#include "runtime_utils.h"

static void DeegenSnippet_UpdateInterpreterTierUpCounterForBranch(CodeBlock* cb, uint8_t* curBytecode, uint8_t* dstBytecode)
{
    // When we do a backward branch (dstBytecode < curBytecode), we should make forward progress to tiering up as
    // we are executing more bytecodes, while the forward branch is opposite (as we skipped executing some bytecodes).
    //
    // The accurate formula should use 'endOfCurBytecode', but for simplicity we just use curBytecode to make computation easier.
    //
    ssize_t diff = curBytecode - dstBytecode;
    cb->m_interpreterTierUpCounter -= diff;
}

DEFINE_DEEGEN_COMMON_SNIPPET("UpdateInterpreterTierUpCounterForBranch", DeegenSnippet_UpdateInterpreterTierUpCounterForBranch)
