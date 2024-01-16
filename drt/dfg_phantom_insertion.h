#pragma once

#include "dfg_node.h"

namespace dfg {

// Run phantom insertion pass. Once this pass is executed, no further transformations to the graph are allowed.
//
// Fo each SSA value that is needed for OSR exit and dies within the block (i.e., not stored to the DFG frame by a SetLocal),
// add Phantom node(s) to make explicit the implicit liveness range where this value is needed by an OSR exit.
// After this pass, it is guaranteed that the last use of an SSA value is also the death of the SSA value for OSR exit purpose.
//
void RunPhantomInsertionPass(Graph* graph);

}   // namespace dfg
