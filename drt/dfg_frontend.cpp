#include "dfg_frontend.h"
#include "dfg_basic_block_builder.h"
#include "dfg_control_flow_and_upvalue_analysis.h"

namespace dfg {

DfgTranslateFunctionResult WARN_UNUSED DfgTranslateFunction(DfgTranslateFunctionContext& tfCtx)
{
    InlinedCallFrame* inlinedCallFrame = tfCtx.m_inlinedCallFrame;
    DfgControlFlowAndUpvalueAnalysisResult cfuvRes = RunControlFlowAndUpvalueAnalysis(tfCtx.m_alloc, inlinedCallFrame->GetCodeBlock());
    tfCtx.m_graph->RegisterBytecodeLivenessInfo(inlinedCallFrame, cfuvRes);
    DfgBuildBasicBlockContext bbCtx(tfCtx, cfuvRes);
    for (size_t bbOrd = 0; bbOrd < bbCtx.m_numPrimBasicBlocks; bbOrd++)
    {
        bbCtx.BuildDfgBasicBlockFromBytecode(bbOrd);
    }
    bbCtx.Finalize();
    TestAssert(bbCtx.m_functionEntry != nullptr);
    return {
        .m_allBBs = std::move(bbCtx.m_allBBs),
        .m_functionEntry = bbCtx.m_functionEntry
    };
}

arena_unique_ptr<Graph> WARN_UNUSED RunDfgFrontend(CodeBlock* codeBlock)
{
    arena_unique_ptr<Graph> graph = Graph::Create(codeBlock);
    TempArenaAllocator alloc;
    DfgTranslateFunctionContext ctx(alloc);
    ctx.m_inlinedCallFrame = InlinedCallFrame::CreateRootFrame(codeBlock, ctx.m_vrState /*inout*/);
    graph->RegisterNewInlinedCallFrame(ctx.m_inlinedCallFrame);
    ctx.m_inlinedCallFrame->InitializeVirtualRegisterUsageArray(ctx.m_vrState.GetVirtualRegisterVectorLength());
    ctx.m_graph = graph.get();
    ctx.m_graph->UpdateTotalNumLocals(ctx.m_vrState.GetVirtualRegisterVectorLength());
    DfgTranslateFunctionResult res = DfgTranslateFunction(ctx);
    TestAssert(res.m_functionEntry != nullptr);
    TestAssert(graph->m_blocks.empty());
    graph->m_blocks.push_back(res.m_functionEntry);
    for (BasicBlock* bb : res.m_allBBs)
    {
        if (bb != res.m_functionEntry)
        {
            graph->m_blocks.push_back(bb);
        }
    }
    TestAssert(graph->m_blocks.size() == res.m_allBBs.size());

    TestAssert(graph->GetTotalNumLocals() == ctx.m_inlinedCallFrame->GetVirtualRegisterVectorLength());

    graph->ComputeReachabilityAndPredecessors();

    // It's possible that we have trivially unreachable basic blocks at this time due to speculative inlining.
    // For example, function 'f' is a dead loop and gets speculatively inlined into our root function,
    // then all the code in the root function after the call becomes trivially unreachable. Remove them.
    //
    graph->RemoveTriviallyUnreachableBlocks();

    return graph;
}

}   // namespace dfg
