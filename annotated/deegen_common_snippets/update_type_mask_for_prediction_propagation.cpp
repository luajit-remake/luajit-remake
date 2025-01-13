#include "define_deegen_common_snippet.h"
#include "drt/dfg_prediction_propagation_util.h"

static void DeegenSnippet_UpdateTypeMaskForPredictionPropagation(TypeMaskTy* dst, TypeMaskTy val, uint8_t* changed)
{
    TypeMaskTy oldVal = *dst;
    TypeMaskTy newVal = oldVal | val;
    *changed |= (oldVal != newVal);
    *dst = newVal;
}

DEFINE_DEEGEN_COMMON_SNIPPET("UpdateTypeMaskForPredictionPropagation", DeegenSnippet_UpdateTypeMaskForPredictionPropagation)

