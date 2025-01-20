#pragma once

#include "common_utils.h"

namespace dfg {

struct Graph;

// Must be executed after prediction propagation pass.
// Decide the DFG variant for each node, and assign speculation for each edge
//
void RunSpeculationAssignmentPass(Graph* graph);

}   // namespace dfg
