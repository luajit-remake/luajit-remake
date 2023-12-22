#include "dfg_ir_validator.h"
#include "temp_arena_allocator.h"
#include "bytecode_builder.h"
#include "dfg_ir_dump.h"

namespace dfg {

using BCKind = DeegenBytecodeBuilder::BCKind;
using BytecodeDecoder = DeegenBytecodeBuilder::BytecodeDecoder;

// Note that the macros below assume that local variable 'graph' is the Graph
//
#define CHECK_REPORT_NODE(expr, node, ...)                                                                  \
    do { if (unlikely(!(expr))) {                                                                           \
        LOG_ERROR(__VA_ARGS__);                                                                             \
        DumpDfgIrGraph(stderr, (graph), DumpIrOptions().ForValidationErrorDump().HighlightNode(node));      \
        return FalseOrNullptr();                                                                            \
    } } while (false)

#define CHECK_REPORT_BLOCK(expr, bb, ...)                                                                   \
    do { if (unlikely(!(expr))) {                                                                           \
        LOG_ERROR(__VA_ARGS__);                                                                             \
        DumpDfgIrGraph(stderr, (graph), DumpIrOptions().ForValidationErrorDump().HighlightBasicBlock(bb));  \
        return FalseOrNullptr();                                                                            \
    } } while (false)

bool WARN_UNUSED ValidateDfgIrGraph(Graph* graph, IRValidateOptions validateOptions)
{
    TempArenaAllocator alloc;

    CHECK_LOG_ERROR(graph->m_blocks.size() > 0, "Missing entry block!");

    TempUnorderedSet<Node*> allNodes(alloc);
    TempUnorderedSet<BasicBlock*> allBBs(alloc);
    for (BasicBlock* bb : graph->m_blocks)
    {
        CHECK_REPORT_BLOCK(bb->m_nodes.size() > 0, bb, "Empty basic blocks are disallowed!");

        // Check that the terminator is valid
        //
        Node* terminatorNode = bb->m_terminator;
        {
            CHECK_REPORT_BLOCK(terminatorNode != nullptr, bb, "Terminator node is nullptr");
            bool foundTerminatorNode = false;
            for (Node* node : bb->m_nodes)
            {
                if (node == terminatorNode)
                {
                    foundTerminatorNode = true;
                    break;
                }
            }
            CHECK_REPORT_BLOCK(foundTerminatorNode, bb, "Terminator node does not refer to a node in the BB");
        }

        // Check that all the non-terminator nodes are straightline (i.e., takes exactly one successor)
        //
        for (Node* node : bb->m_nodes)
        {
            if (node == terminatorNode)
            {
                continue;
            }
            CHECK_REPORT_NODE(node->GetNumNodeControlFlowSuccessors() == 1, node, "Terminator node showed up at a non-terminal position!");
        }

        if (terminatorNode->IsReturnNode())
        {
            CHECK_REPORT_NODE(terminatorNode->GetNumNodeControlFlowSuccessors() == 0, terminatorNode, "Return nodes should have 0 successors!");
        }

        // Check that the terminal node's # of destinations matches the number of successors of this BB
        //
        CHECK_REPORT_NODE(terminatorNode->GetNumNodeControlFlowSuccessors() == bb->GetNumSuccessors(), terminatorNode, "Wrong number of successor blocks");

        // If the basic block has 0 or 1 successor, the terminator node should be at terminal position
        //
        if (bb->GetNumSuccessors() <= 1)
        {
            CHECK_REPORT_NODE(terminatorNode == bb->m_nodes.back(), terminatorNode, "Terminator node should be the last node if BB has 0 or 1 successors");
        }

        TempUnorderedSet<Node*> nodesInBB(alloc);
        Node* currentVarResGeneratorNode = nullptr;
        bool hasSeenVarResGeneratorNode = false;
        for (Node* node : bb->m_nodes)
        {
            CHECK_REPORT_NODE(!node->IsConstantLikeNode(), node, "Constant-like nodes should not show up in a basic block!");
            CHECK_REPORT_NODE(!allNodes.count(node), node, "A node showed up multiple times in the graph");
            allNodes.insert(node);

            if (node->MayOsrExit())
            {
                CHECK_REPORT_NODE(node->IsExitOK(), node, "A node that may exit showed up in a position where OSR exit is not allowed");
            }

            // Validate that the node in each BB only reference nodes defined in the same BB earlier
            //
            for (uint32_t inputOrd = 0; inputOrd < node->GetNumInputs(); inputOrd++)
            {
                Edge e = node->GetInputEdge(inputOrd);
                Node* inputNode = e.GetOperand();
                if (!inputNode->IsConstantLikeNode())
                {
                    CHECK_REPORT_NODE(nodesInBB.count(inputNode), node, "Input edge referenced a node not defined in the same basic block");
                }
                size_t ieOrd = e.GetOutputOrdinal();
                if (ieOrd == 0)
                {
                    CHECK_REPORT_NODE(inputNode->HasDirectOutput(), node, "Invalid output ordinal reference for input ord %u", static_cast<unsigned int>(inputOrd));
                }
                else
                {
                    CHECK_REPORT_NODE(ieOrd <= inputNode->GetNumExtraOutputs(), node, "Invalid output ordinal reference for input ord %u", static_cast<unsigned int>(inputOrd));
                }
            }

            if (node->IsNodeAccessesVR())
            {
                if (currentVarResGeneratorNode == nullptr)
                {
                    CHECK_REPORT_NODE(!hasSeenVarResGeneratorNode, node, "Invalid VariadicResult reference (VariadicResult already clobbered)");
                    CHECK_REPORT_NODE(node->IsPrependVariadicResNode() && node->GetNumInputs() == 0 && node->GetVariadicResultInputNode() == nullptr,
                                      node, "Invalid VariadicResult reference");
                }
                else
                {
                    Node* vrInputNode = node->GetVariadicResultInputNode();
                    CHECK_REPORT_NODE(vrInputNode == currentVarResGeneratorNode, node, "VariadicResult does not point to most recent generator");
                }
            }
            if (node->IsNodeClobbersVR())
            {
                currentVarResGeneratorNode = nullptr;
            }
            if (node->IsNodeGeneratesVR())
            {
                currentVarResGeneratorNode = node;
                hasSeenVarResGeneratorNode = true;
            }
            TestAssert(!nodesInBB.count(node));
            nodesInBB.insert(node);
        }
        CHECK_REPORT_BLOCK(!allBBs.count(bb), bb, "A basic block showed up multiple times in the graph");
        allBBs.insert(bb);
    }

    // Check that all the successor values refer to valid basic blocks
    //
    for (BasicBlock* bb : graph->m_blocks)
    {
        CHECK_REPORT_BLOCK(bb->GetNumSuccessors() <= 2, bb, "Invalid number of successors");
        for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
        {
            BasicBlock* succ = bb->m_successors[i];
            CHECK_REPORT_BLOCK(allBBs.count(succ), bb, "Invalid successor basic block for ord %u", static_cast<unsigned int>(i));
            CHECK_REPORT_BLOCK(succ != graph->GetEntryBB(), bb, "It is not allowed for a block to branch to the entry block");
        }
    }

    // Check that all the basic blocks are reachable
    //
    if (!validateOptions.allowUnreachableBlocks)
    {
        TempQueue<BasicBlock*> q(alloc);
        auto pushQueue = [&](BasicBlock* bb) ALWAYS_INLINE
        {
            if (allBBs.count(bb))
            {
                allBBs.erase(allBBs.find(bb));
                q.push(bb);
            }
        };
        pushQueue(graph->m_blocks[0]);

        while (!q.empty())
        {
            BasicBlock* bb = q.front();
            q.pop();
            for (size_t i = 0; i < bb->GetNumSuccessors(); i++)
            {
                pushQueue(bb->m_successors[i]);
            }
        }

        // The IR dump already includes information about the block predecessors, so no need to highlight
        // (if we want to highlight, we should find and highlight a block with no pred, not a random unreachable block, which is clumsy)
        //
        CHECK_REPORT_BLOCK(allBBs.size() == 0, nullptr, "Graph contains unreachable basic blocks");
    }

    return true;
}

}   // namespace dfg
