#include "define_deegen_common_snippet.h"
#include "dfg_codegen_register_renamer.h"

static void DeegenSnippet_ApplyDfgRuntimeRegPatchData(uint8_t* code, dfg::RegAllocStateForCodeGen* regConfig, uint16_t* encodedPatchDataStream)
{
    dfg::RunStencilRegisterPatchingPhase(code, regConfig, encodedPatchDataStream);
}

DEFINE_DEEGEN_COMMON_SNIPPET("ApplyDfgRuntimeRegPatchData", DeegenSnippet_ApplyDfgRuntimeRegPatchData)

