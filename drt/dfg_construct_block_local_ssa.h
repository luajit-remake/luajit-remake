#pragma once

#include "common_utils.h"
#include "temp_arena_allocator.h"
#include "dfg_node.h"

namespace dfg {

// Called on the initial IR graph. It does the following:
// 1. Allocate and initialize all the states needed for block-local SSA
// 2. Canonicalize GetLocals and SetLocals
// 3. Construct block-local SSA
// 4. Clean up dead GetLocals, dead SetLocals, and GetLocals that would trivially yield UndefValue
// 5. Setup logical variable for each GetLocal/SetLocal
// The input graph should be in PreUnification form, and this pass upgrades it to BlockLocalSSA form.
//
void InitializeBlockLocalSSAFormAndSetupLogicalVariables(Graph* graph);

// Canonicalize and clean up GetLocals and SetLocals, and reconstruct block-local SSA after a
// transformation degraded the graph to load-store form.
//
void ReconstructBlockLocalSSAFormFromLoadStoreForm(Graph* graph);

}   // namespace dfg
