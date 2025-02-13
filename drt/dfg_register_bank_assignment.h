#pragma once

#include "common_utils.h"

namespace dfg {

struct Graph;

// For each node that participates in reg alloc and allows us to choose whether some input/output operands should be in GPR or FPR,
// make that choice with the goal of reducing the amount of GPR <-> FPR moves. Our strategy is not optimal, only heuristic.
//
// After the pass, all operands and outputs, except the fixed-part operands of the guest-language nodes that does not support reg alloc
// and the outputs that are born on the stack, are assigned a GPR/FPR choice, designating which register bank this use should choose.
//
void RunRegisterBankAssignmentPass(Graph* graph);

}   // namespace dfg
