#pragma once

#include "common_utils.h"
#include "tvalue.h"

namespace dfg {

// The propagation rule for GetLocal and GetCapturedVar:
// the result prediction is simply the prediction for the LogicalVariable
//
struct GetVariablePropagationRule
{
    __attribute__((__packed__)) TypeMaskTy* m_variablePrediction;
    TypeMaskTy m_prediction;

    bool WARN_UNUSED Run()
    {
        TypeMaskTy newMask = m_prediction | *m_variablePrediction;
        bool changed = (newMask != m_prediction);
        m_prediction = newMask;
        return changed;
    }
};
static_assert(alignof(GetVariablePropagationRule) == alignof(TypeMaskTy));

using NodePredictionPropagationFn = bool(*)(void*);

// Note that the nodes in a basic block can be divided into three categories:
//     1. GetLocal and GetCapturedVar:
//          The prediction of these nodes is the prediction of the logical variables, which may be affected
//          by other basic blocks. These are the only nodes whose prediction are directly affected by other basic blocks.
//     2. Constant-prediction nodes:
//          The prediction of these nodes never changes, so we only need to compute them once and we are done.
//     3. Nodes that does not directly or indirectly use GetLocal and GetCaptureVar:
//          The prediction of these nodes are never affected by the other basic blocks.
//          So similar to constant-prediction nodes, we only need to compute them once.
//     4. Nodes that directly or indirectly use use GetLocal and GetCaptureVar:
//          These nodes need to be recomputed if any of the GetLocal/GetCaptureVar prediction changed.
//
// So we can propagate all GetLocal/GetCapturedVar in the basic block first.
// If none of them changed, we can skip this basic block. Otherwise, we propagate node category (4) above.
//
// GetCapturedVar is a bit tricky since GetCapturedVar and SetCapturedVar cannot be trivially load/store-forwarded,
// so a GetCapturedVar may see result of a SetCapturedVar in the same block.
// Fortunately, one can see that if we compute the GetCapturedVar with a private m_prediction field (or in other words,
// make a clone of the GetCapturedVar at block start), the rule would work correctly.
//
struct BasicBlockPropagationRules
{
    GetVariablePropagationRule* m_gvRules;
    GetVariablePropagationRule* m_gvRulesEnd;

    struct NodeOp
    {
        NodePredictionPropagationFn m_func;
        void* m_data;
    };

    NodeOp* m_nodeRules;
    NodeOp* m_nodeRulesEnd;

    bool WARN_UNUSED Run()
    {
        bool changed = false;

        // Update the GetLocal and GetCapturedVar predictions
        //
        {
            GetVariablePropagationRule* rule = m_gvRules;
            GetVariablePropagationRule* ruleEnd = m_gvRulesEnd;
            while (rule < ruleEnd)
            {
                changed |= rule->Run();
                rule++;
            }
        }
        if (!changed)
        {
            // None of the GetLocal or GetCapturedVar changed, no need to run the rest
            //
            return false;
        }

        // Update prediction for each node in the BB
        //
        {
            NodeOp* rule = m_nodeRules;
            NodeOp* ruleEnd = m_nodeRulesEnd;
            while (rule < ruleEnd)
            {
                changed |= rule->m_func(rule->m_data);
                rule++;
            }
        }
        return changed;
    }
};

}   // namespace dfg
