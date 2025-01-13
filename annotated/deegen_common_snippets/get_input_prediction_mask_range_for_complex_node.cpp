#include "define_deegen_common_snippet.h"
#include "drt/dfg_prediction_propagation_util.h"

static TypeMaskTy** DeegenSnippet_GetInputPredictionMaskRangeForComplexNode(DfgComplexNodePredictionPropagationData* data, size_t rangeStartOrd)
{
    return data->m_inputMaskAddrs + rangeStartOrd;
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetInputPredictionMaskRangeForComplexNode", DeegenSnippet_GetInputPredictionMaskRangeForComplexNode)

