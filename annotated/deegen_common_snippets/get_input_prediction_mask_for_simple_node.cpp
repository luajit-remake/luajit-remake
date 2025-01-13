#include "define_deegen_common_snippet.h"
#include "drt/dfg_prediction_propagation_util.h"

static TypeMaskTy DeegenSnippet_GetInputPredictionMaskForSimpleNode(DfgSimpleNodePredictionPropagationData* data, size_t inputOrd)
{
    return *data->GetInputMaskAddr(inputOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetInputPredictionMaskForSimpleNode", DeegenSnippet_GetInputPredictionMaskForSimpleNode)

