#include "dfg_frontend.h"
#include "dfg_basic_block_builder.h"
#include "dfg_control_flow_and_upvalue_analysis.h"
#include "dfg_construct_block_local_ssa.h"
#include "dfg_trivial_cfg_cleanup.h"
#include "dfg_ir_validator.h"

namespace dfg {

DfgTranslateFunctionResult WARN_UNUSED DfgTranslateFunction(DfgTranslateFunctionContext& tfCtx)
{
    InlinedCallFrame* inlinedCallFrame = tfCtx.m_inlinedCallFrame;
    DfgControlFlowAndUpvalueAnalysisResult cfuvRes = RunControlFlowAndUpvalueAnalysis(tfCtx.m_alloc, inlinedCallFrame->GetCodeBlock());
    tfCtx.m_graph->RegisterBytecodeLivenessInfo(inlinedCallFrame, cfuvRes);
    // TODO: investigate if we should build the DFG basic blocks in bytecodeIndex order,
    // or to pass down this information to the DFG basic blocks so we can sort them based on natural order or bytecodeIndex order later
    // The basic block order have quite some impact on how fast fixpoint algorithms converge
    //
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
    ctx.m_graph->UpdateTotalNumInterpreterSlots(ctx.m_inlinedCallFrame->GetInterpreterSlotForFrameEnd().Value());
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

    if (x_run_validation_after_each_pass_in_test_build)
    {
        TestAssert(ValidateDfgIrGraph(graph.get(), IRValidateOptions().SetAllowUnreachableBlocks()));
    }

    RunCleanupControlFlowGraphPass(graph.get());
    TestAssertImp(x_run_validation_after_each_pass_in_test_build, ValidateDfgIrGraph(graph.get()));

    InitializeBlockLocalSSAFormAndSetupLogicalVariables(graph.get());
    TestAssertImp(x_run_validation_after_each_pass_in_test_build, ValidateDfgIrGraph(graph.get()));

    return graph;
}

}   // namespace dfg
