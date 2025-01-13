#include "define_deegen_common_snippet.h"
#include "drt/dfg_prediction_propagation_util.h"

static TypeMaskTy DeegenSnippet_GetInputPredictionMaskForComplexNode(DfgComplexNodePredictionPropagationData* data, size_t inputOrd)
{
    return *(data->m_inputMaskAddrs[inputOrd]);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetInputPredictionMaskForComplexNode", DeegenSnippet_GetInputPredictionMaskForComplexNode)

