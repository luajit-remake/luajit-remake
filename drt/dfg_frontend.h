#pragma once

#include "dfg_node.h"
#include "runtime_utils.h"
#include "temp_arena_allocator.h"

namespace dfg
{

// The DFG frontend that builds up the DFG graph from bytecode stream.
//
// Currently, the frontend is also responsible for speculative inlining.
// There are edge cases where this design does not produce optimal result, for example:
//     function bar(f) f() end
//     function foo() bar(baz) end
//     function foo2() bar(baz2) bar(baz3) bar(baz4) ... end
//
// In the above example, when 'bar' is called by 'foo', 'f' is always 'baz'.
// But when 'bar' is called by 'foo2', 'f' could be a bunch of different functions.
// This pollutes the call IC of 'f()' and make it contain a bunch of different targets.
//
// When the DFG frontend builds DFG graph for 'foo', it will speculatively inline 'bar', but will not
// be able to deduce from the IC that the likely callee of 'f' is 'baz', so 'baz' will not be inlined.
// This is a case where the "frontend responsible for inlining" design fail to produce optimal result.
// In fact, later DFG stages will even be able to prove that 'f' is always 'baz', but at which point
// inlining is already not possible.
//
// However, this "frontend responsible for inlining" design is the simplest to start with, and is also
// the design used by JSC, so we will go with it.
//
struct DfgTranslateFunctionContext
{
    // This only sets up the members with a non-empty constructor
    //
    DfgTranslateFunctionContext(TempArenaAllocator& alloc)
        : m_alloc(alloc)
        , m_graph(nullptr)
        , m_inlinedCallFrame(nullptr)
        , m_vrState(alloc)
    { }

    TempArenaAllocator& m_alloc;
    Graph* m_graph;
    InlinedCallFrame* m_inlinedCallFrame;
    VirtualRegisterAllocator m_vrState;
};

struct DfgTranslateFunctionResult
{
    TempVector<BasicBlock*> m_allBBs;
    BasicBlock* m_functionEntry;
};

DfgTranslateFunctionResult WARN_UNUSED DfgTranslateFunction(DfgTranslateFunctionContext& tfCtx);

arena_unique_ptr<Graph> WARN_UNUSED RunDfgFrontend(CodeBlock* codeBlock);

}   // namespace dfg
