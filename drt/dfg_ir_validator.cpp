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

        if (bb == graph->GetEntryBB())
        {
            CHECK_REPORT_BLOCK(bb->m_bcForInterpreterStateAtBBStart.IsInvalid(), bb, "root block should have invalid bcAtBBStart");
        }
        else
        {
            CHECK_REPORT_BLOCK(!bb->m_bcForInterpreterStateAtBBStart.IsInvalid(), bb, "non-root block should have a valid bcAtBBStart");
        }

        // Check that the terminator is valid
        //
        Node* terminatorNode = bb->GetTerminator();
        size_t terminatorNodeIndex = static_cast<size_t>(-1);
        {
            CHECK_REPORT_BLOCK(terminatorNode != nullptr, bb, "Terminator node is nullptr");
            bool foundTerminatorNode = false;
            for (size_t idx = 0; idx < bb->m_nodes.size(); idx++)
            {
                Node* node = bb->m_nodes[idx];
                if (node == terminatorNode)
                {
                    foundTerminatorNode = true;
                    terminatorNodeIndex = static_cast<size_t>(idx);
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

        // If the basic block has 0 successor, all nodes after the terminator node should be Phantom nodes
        //
        if (bb->GetNumSuccessors() == 0)
        {
            for (size_t idx = terminatorNodeIndex + 1; idx < bb->m_nodes.size(); idx++)
            {
                Node* node = bb->m_nodes[idx];
                CHECK_REPORT_NODE(node->IsPhantomNode(), terminatorNode, "For basic block with 0 successors, only phantom nodes are allowed to follow terminator node");
            }
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
                Edge& e = node->GetInputEdge(inputOrd);
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
            BasicBlock* succ = bb->GetSuccessor(i);
            CHECK_REPORT_BLOCK(allBBs.count(succ), bb, "Invalid successor basic block for ord %u", static_cast<unsigned int>(i));
            CHECK_REPORT_BLOCK(succ != graph->GetEntryBB(), bb, "It is not allowed for a block to branch to the entry block");
        }
    }

    // Check that the SetLocal and ShadowStore form a consistent state at the end of every basic block
    //
    for (BasicBlock* bb : graph->m_blocks)
    {
        TempUnorderedMap<size_t /*interpreterSlot*/, Node*> shadowStoreWrites(alloc);
        TempUnorderedMap<size_t /*interpreterSlot*/, Node*> setLocalWrites(alloc);

        auto getShadowStoreValue = [&](Node* node)
        {
            if (node->IsShadowStoreNode())
            {
                return node->GetSoleInput().GetValue();
            }
            else
            {
                TestAssert(node->IsShadowStoreUndefToRangeNode());
                return graph->GetUndefValue();
            }
        };

        for (Node* node : bb->m_nodes)
        {
            if (node->IsSetLocalNode())
            {
                InterpreterSlot slot = node->GetLocalOperationInterpreterSlotSlow();
                CHECK_REPORT_NODE(shadowStoreWrites.count(slot.Value()),
                                  node,
                                  "SetLocal is not preceded by a ShadowStore on the same location");
                {
                    Node* shadowStoreNode = shadowStoreWrites[slot.Value()];
                    Value setLocalValue = node->GetSoleInput().GetValue();
                    Value shadowStoreValue = getShadowStoreValue(shadowStoreNode);
                    CHECK_REPORT_NODE(setLocalValue.IsIdenticalAs(shadowStoreValue),
                                      node,
                                      "SetLocal is not preceded by a ShadowStore writing the same value");
                }

                bool isSetLocalLiveAtTail = bb->IsSetLocalBytecodeLiveAtTail(node);
                if (graph->IsBlockLocalSSAForm())
                {
                    CHECK_REPORT_NODE(isSetLocalLiveAtTail, node, "In block-local SSA form, all SetLocal should live at tail");
                }
                if (isSetLocalLiveAtTail)
                {
                    VirtualRegisterMappingInfo vrmi = bb->GetVirtualRegisterForInterpreterSlotAtTail(slot);
                    TestAssert(vrmi.IsLive());
                    TestAssert(!vrmi.IsUmmapedToAnyVirtualReg());
                    TestAssert(vrmi.GetVirtualRegister().Value() == node->GetLocalOperationVirtualRegisterSlow().Value());
                    setLocalWrites[slot.Value()] = node;
                    std::ignore = vrmi;
                }
            }
            if (node->IsShadowStoreNode())
            {
                InterpreterSlot slot = node->GetShadowStoreInterpreterSlotOrd();
                shadowStoreWrites[slot.Value()] = node;
            }
            else if (node->IsShadowStoreUndefToRangeNode())
            {
                InterpreterSlot slotStart = node->GetShadowStoreUndefToRangeStartInterpSlotOrd();
                size_t numSlots = node->GetShadowStoreUndefToRangeRangeLength();
                for (size_t i = 0; i < numSlots; i++)
                {
                    shadowStoreWrites[slotStart.Value() + i] = node;
                }
            }
        }

        for (auto& it : setLocalWrites)
        {
            InterpreterSlot slot = InterpreterSlot(it.first);
            Node* setLocalNode = it.second;
            TestAssert(setLocalNode->IsSetLocalNode());
            Value setLocalVal = setLocalNode->GetSoleInput().GetValue();

            CHECK_REPORT_NODE(shadowStoreWrites.count(slot.Value()),
                              setLocalNode,
                              "SetLocal disagrees with ShadowStore at BB end (no ShadowStore found)");

            Node* shadowStoreNode = shadowStoreWrites[slot.Value()];
            Value shadowStoreVal = getShadowStoreValue(shadowStoreNode);

            CHECK_REPORT_NODE(setLocalVal.IsIdenticalAs(shadowStoreVal),
                              setLocalNode,
                              "SetLocal disagrees with ShadowStore at BB end (ShadowStore value different from SetLocal value");
        }

        for (auto& it : shadowStoreWrites)
        {
            InterpreterSlot slot = InterpreterSlot(it.first);
            Node* shadowStoreNode = it.second;
            Value shadowStoreVal = getShadowStoreValue(shadowStoreNode);

            VirtualRegisterMappingInfo vrmi = bb->GetVirtualRegisterForInterpreterSlotAtTail(slot);
            if (!vrmi.IsLive())
            {
                // This is not a comprehensive assert: it's still possible that this value
                // becomes dead in a successor block. We will not try to spend more effort check it here,
                // since the OSR stackmap builder should be able to figure this out and fire assertion anyway.
                //
                Node* shadowStoreValNode = shadowStoreVal.GetOperand();
                CHECK_REPORT_NODE(!shadowStoreValNode->IsCreateCapturedVarNode(),
                                  shadowStoreNode,
                                  "A slot storing an CapturedVar must be live (did the bytecode properly emit UpvalueClose?)");
                continue;
            }

            if (vrmi.IsUmmapedToAnyVirtualReg())
            {
                CHECK_REPORT_NODE(shadowStoreVal.IsConstantValue(),
                                  shadowStoreNode,
                                  "A ShadowStore to an interpreter slot not mapped to any VirtualRegister must be a constant value");

                Node* constantOperand = shadowStoreVal.GetOperand();
                CHECK_REPORT_NODE(constantOperand == vrmi.GetConstantValue(),
                                  shadowStoreNode,
                                  "The constant value written by a ShadowStore to an interpreter slot not mapped to any VirtualRegister "
                                  "must agree with the constant value for that interpreter slot");
                continue;
            }

            CHECK_REPORT_NODE(setLocalWrites.count(slot.Value()),
                              shadowStoreNode,
                              "ShadowStore disagrees with SetLocal at BB end (no SetLocal found)");

            {
                Node* setLocalNode = setLocalWrites[slot.Value()];
                TestAssert(setLocalNode->IsSetLocalNode());
                Value setLocalVal = setLocalNode->GetSoleInput().GetValue();

                CHECK_REPORT_NODE(setLocalVal.IsIdenticalAs(shadowStoreVal),
                                  shadowStoreNode,
                                  "ShadowStore disagrees with SetLocal at BB end (ShadowStore value different from SetLocal value");
            }
        }

        // For each unmapped interpreter slot at BB end, one of the following should be true:
        // 1. It is also unmapped at BB start with same value
        // 2. There is a ShadowStore that sets this slot to the correct value
        //
        for (size_t interpSlot = 0; interpSlot < graph->GetTotalNumInterpreterSlots(); interpSlot++)
        {
            VirtualRegisterMappingInfo vrmi = bb->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(interpSlot));
            if (vrmi.IsLive() && vrmi.IsUmmapedToAnyVirtualReg())
            {
                // Note that if 'interpSlot' is found in shadowStoreWrites, the assertion has been done in the earlier loop
                // So we only need to check the not-found case here
                //
                if (!shadowStoreWrites.count(interpSlot))
                {
                    VirtualRegisterMappingInfo vrmiAtHead = bb->GetVirtualRegisterForInterpreterSlotAtHead(InterpreterSlot(interpSlot));
                    CHECK_REPORT_BLOCK(vrmiAtHead.IsLive() && vrmiAtHead.IsUmmapedToAnyVirtualReg(),
                                       bb,
                                       "No ShadowStore to a interpreter slot that becomes unmapped during the BB!");
                    CHECK_REPORT_BLOCK(vrmiAtHead.GetConstantValue() == vrmi.GetConstantValue(),
                                       bb,
                                       "No ShadowStore to a interpreter slot that becomes unmapped with a different constant value during the BB!");
                }
            }
        }
    }

    if (graph->IsCfgAvailable())
    {
        CHECK_REPORT_BLOCK(graph->CheckCfgConsistent(), nullptr, "CFG predecessor / successor info is broken");
    }

    // This is bad, since the assertion is only done in test build.. but for now..
    //
#ifdef TESTBUILD
    for (BasicBlock* bb : graph->m_blocks)
    {
        bb->AssertVirtualRegisterMappingConsistentAtTail();
    }
#endif

    // Check that branch-dest OSR exit destination should only show up at the end of the block
    //
    for (BasicBlock* bb : graph->m_blocks)
    {
        bool seenBranchyDest = false;
        OsrExitDestination branchyDest;
        for (Node* node : bb->m_nodes)
        {
            OsrExitDestination exitDest = node->GetOsrExitDest();
            if (seenBranchyDest)
            {
                CHECK_REPORT_NODE(exitDest == branchyDest, node, "The exit dest should be the same after a branchy OSR exit");
            }
            else if (exitDest.IsBranchDest())
            {
                CHECK_REPORT_NODE(bb->GetNumSuccessors() == 2, node, "Branchy OSR exit dest should only show up in a block with 2 successors");
                seenBranchyDest = true;
                branchyDest = exitDest;
            }
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
