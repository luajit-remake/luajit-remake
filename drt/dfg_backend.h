#pragma once

#include "common_utils.h"
#include "dfg_stack_layout_planning.h"
#include "runtime_utils.h"

namespace dfg {

struct Graph;

struct DfgBackendResult
{
    DfgCodeBlock* m_dfgCodeBlock;
};

// Run the DFG backend pipeline: register allocation, code generation, OSR exit map generation
//
DfgBackendResult WARN_UNUSED RunDfgBackend(Graph* graph, StackLayoutPlanningResult& stackLayoutPlanningResult);

}   // namespace dfg
