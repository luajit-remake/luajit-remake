#include "define_deegen_common_snippet.h"
#include "dfg_slowpath_register_config_helper.h"

static void DeegenSnippet_ApplyDfgRuntimeRegPatchDataUsingSlowPathDataRegConfig(uint8_t* code, uint8_t* slowPathDataRegConfig, uint16_t* encodedPatchDataStream)
{
    dfg::RunStencilRegisterPatchingPhaseUsingRegInfoFromSlowPathData(code, slowPathDataRegConfig, encodedPatchDataStream);
}

DEFINE_DEEGEN_COMMON_SNIPPET("ApplyDfgRuntimeRegPatchDataUsingSlowPathDataRegConfig", DeegenSnippet_ApplyDfgRuntimeRegPatchDataUsingSlowPathDataRegConfig)

