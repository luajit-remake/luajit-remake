#pragma once

#include "common_utils.h"

namespace dfg {

struct Graph;

struct DfgBackendResult
{

};

// Run the DFG backend pipeline: register allocation, code generation, OSR exit map generation
//
DfgBackendResult WARN_UNUSED RunDfgBackend(Graph* graph);

}   // namespace
