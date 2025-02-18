#pragma once

#include "common_utils.h"
#include "dfg_stack_layout_planning.h"
#include "runtime_utils.h"

namespace dfg {

struct Graph;

struct DfgBackendResult
{
    DfgCodeBlock* m_dfgCodeBlock;
#ifdef TESTBUILD
    // Human-readable description of what JIT code is generated
    //
    char* m_codegenLogDump;
    size_t m_codegenLogDumpSize;    // excluding terminating NULL
#endif
};

// Run the DFG backend pipeline: register allocation, code generation, OSR exit map generation
//
DfgBackendResult WARN_UNUSED RunDfgBackend(TempArenaAllocator& resultAlloc, Graph* graph, StackLayoutPlanningResult& stackLayoutPlanningResult);

}   // namespace dfg
