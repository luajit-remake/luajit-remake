#include "dfg_trivial_cfg_cleanup.h"
#include "temp_arena_allocator.h"

namespace dfg {

namespace {

struct DfgTrivialCfgCleanupPass
{
    Graph* m_graph;

    DfgTrivialCfgCleanupPass(Graph* graph)
        : m_graph(graph)
    { }

    BasicBlock* GetReplacement(BasicBlock* bb)
    {
        if (bb->m_replacement == bb)
        {
            return bb;
        }
        BasicBlock* result = GetReplacement(bb->m_replacement);
        bb->m_replacement = result;
        return result;
    }

    // Check if the BB consists of a single NOP node with no inputs (the frontend uses this pattern when
    // it needs to create an empty BB for convenience)
    //
    bool IsTriviallyEmptyBlock(BasicBlock* bb)
    {
        if (bb->m_nodes.size() != 1)
        {
            return false;
        }
        Node* node = bb->m_nodes[0];
        return node->IsNoopNode() && node->GetNumInputs() == 0;
    }

    bool BlockCanBeMergedIntoPredecessor(BasicBlock* bb)
    {
        if (bb->m_predecessors.size() != 1)
        {
            return false;
        }
        BasicBlock* pred = bb->m_predecessors[0];
        TestAssert(pred->GetNumSuccessors() > 0);
        TestAssertImp(pred->GetNumSuccessors() == 1, pred->GetSuccessor(0) == bb);
        return pred->GetNumSuccessors() == 1;
    }

    void Run()
    {
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            bb->m_replacement = bb;
        }

        bool hasEmptyBlocks = false;
        for (size_t idx = 1 /*skipEntryBB*/, numBlocks = m_graph->m_blocks.size();
             idx < numBlocks;
             idx++)
        {
            BasicBlock* bb = m_graph->m_blocks[idx];
            if (IsTriviallyEmptyBlock(bb))
            {
                TestAssert(bb->GetNumSuccessors() == 1);
                BasicBlock* succ = bb->GetSuccessor(0);
                // Note that we cannot remove empty block that branches to itself (a trivial dead loop).
                //
                if (succ != bb)
                {
                    bb->m_replacement = succ;
                    hasEmptyBlocks = true;
                }
            }
        }

        if (hasEmptyBlocks)
        {
            for (BasicBlock* bb : m_graph->m_blocks)
            {
                if (bb->m_replacement != bb)
                {
                    // This block itself is going to be replaced, don't bother
                    //
                    // This is required: otherwise in the rare case where there is a loop where all basic blocks
                    // are empty (of course this loop won't even be reachable from the entry block, but in theory
                    // it can happen...), we will run into a dead loop in GetReplacement()
                    //
                    continue;
                }
                size_t numSuccessors = bb->GetNumSuccessors();
                for (size_t succIdx = 0; succIdx < numSuccessors; succIdx++)
                {
                    BasicBlock* succBlock = bb->GetSuccessor(succIdx);
                    BasicBlock* replacement = GetReplacement(succBlock);
                    TestAssert(replacement != nullptr);
                    if (replacement != succBlock)
                    {
                        bb->m_successors[succIdx] = replacement;
                    }
                }
            }
        }

        m_graph->ComputeReachabilityAndPredecessors();

        // It's worth mentioning that even if no empty block exists, it's still possible that we have unreachable
        // basic blocks due to speculative inlining.
        //
        // For example, function 'f' is a dead loop and gets speculatively inlined into our root function,
        // then all the code in the root function after the call becomes unreachable.
        //
        m_graph->RemoveTriviallyUnreachableBlocks();

#ifdef TESTBUILD
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            // All the blocks that we marked for replacement should have been gone now
            //
            TestAssert(bb->m_replacement == bb);
            bb->AssertVirtualRegisterMappingConsistentAtTail();
        }
#endif

        // We can merge a basic block into its unique predecessor block if it is also its unique predecessor's unique suceesor
        //
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            // We use m_replacement == null to mark that this block has been merged away and needs to be deleted
            //
            if (bb->m_replacement.IsNull())
            {
                continue;
            }
            if (BlockCanBeMergedIntoPredecessor(bb))
            {
                // This block itself can be merged into predecessor, not a maximum chain
                //
                continue;
            }
            // Now we know bb cannot be merged into its predecessor, try to merge successor block into bb if possible
            //
            while (bb->GetNumSuccessors() == 1)
            {
                BasicBlock* succ = bb->GetSuccessor(0);
                TestAssert(!succ->m_replacement.IsNull());
                TestAssertIff(BlockCanBeMergedIntoPredecessor(succ), succ->m_predecessors.size() == 1);
                if (succ->m_predecessors.size() == 1)
                {
                    bb->MergeSuccessorBlock(succ);
                    succ->m_replacement = nullptr;
                }
                else
                {
                    break;
                }
            }
        }

        // Remove all the deleted blocks
        //
        {
            TestAssert(!m_graph->GetEntryBB()->m_replacement.IsNull());
            size_t newCount = 0;
            for (BasicBlock* bb : m_graph->m_blocks)
            {
                if (bb->m_replacement.IsNull())
                {
                    continue;
                }
                m_graph->m_blocks[newCount] = bb;
                newCount++;
            }
            m_graph->m_blocks.resize(newCount);
        }

#ifdef TESTBUILD
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            TestAssert(bb->m_replacement == bb);
            bb->AssertVirtualRegisterMappingConsistentAtTail();
        }
#endif

        TestAssert(m_graph->CheckCfgConsistent());

        if (m_graph->IsBlockLocalSSAForm())
        {
            m_graph->DegradeToLoadStoreForm();
        }
    }
};

}   // anonymous namespace

void RunCleanupControlFlowGraphPass(Graph* graph)
{
    TestAssert(graph->IsPreUnificationForm());
    DfgTrivialCfgCleanupPass pass(graph);
    pass.Run();
    TestAssert(graph->IsCfgAvailable());
}

}   // namespace dfg
