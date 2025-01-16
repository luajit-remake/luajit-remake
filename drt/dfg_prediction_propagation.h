#pragma once

#include "common_utils.h"
#include "temp_arena_allocator.h"
#include "tvalue.h"

namespace dfg {

struct Graph;

// The prediction propagation result
// In addition to the fields below, each Node that has output is assigned a predictions field
// containing the predictions of all its outputs
//
struct PredictionPropagationResult
{
    // An array of length graph->GetAllLogicalVariables().size()
    // The prediction mask for each logical variable
    //
    TypeMaskTy* m_varPredictions;

    // An array of length graph->GetAllCapturedVarLogicalVariables().size()
    // The prediction mask for each logical captured variable
    //
    TypeMaskTy* m_capturedVarPredictions;

    // The number of nodes with output (so they are assigned predictions)
    //
    size_t m_totalNodesWithPredictions;
};

// 'alloc' must be kept alive to keep all the prediction results valid
//
PredictionPropagationResult WARN_UNUSED RunPredictionPropagation(TempArenaAllocator& alloc, Graph* graph);

// For testing only, instead of looking at the real value profile,
// all value profiles are treated as if they have seen tBoxedValueTop
//
PredictionPropagationResult WARN_UNUSED RunPredictionPropagationWithoutValueProfile(TempArenaAllocator& alloc, Graph* graph);

}   // namespace dfg
