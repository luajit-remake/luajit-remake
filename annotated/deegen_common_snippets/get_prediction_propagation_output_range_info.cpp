#include "define_deegen_common_snippet.h"
#include "drt/dfg_prediction_propagation_util.h"

static DfgComplexNodePredictionPropagationData::RangeInfo* DeegenSnippet_GetRangeInfoForPredictionPropagationComplexNode(DfgComplexNodePredictionPropagationData* data, size_t rangeInfoOrd)
{
    return data->GetRangeInfo(rangeInfoOrd);
}

DEFINE_DEEGEN_COMMON_SNIPPET("GetRangeInfoForPredictionPropagationComplexNode", DeegenSnippet_GetRangeInfoForPredictionPropagationComplexNode)

