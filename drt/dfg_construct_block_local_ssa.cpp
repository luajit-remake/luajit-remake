#include "common_utils.h"
#include "dfg_arena.h"
#include "temp_arena_allocator.h"
#include "dfg_node.h"

namespace dfg {

struct ConstructBlockLocalSSAPass
{
    TempArenaAllocator m_alloc;

    Graph* m_graph;

    ConstructBlockLocalSSAPass(Graph* graph)
        : m_alloc()
        , m_graph(graph)
    { }

    void AllocateStateArraysForLocals()
    {
        TestAssert(m_graph->IsPreUnificationForm());
        uint32_t numLocals = m_graph->GetTotalNumLocals();
        TestAssert(numLocals > 0);
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            TestAssert(bb->m_numLocals == static_cast<uint32_t>(-1));
            TestAssert(bb->m_localInfoAtHead == nullptr && bb->m_localInfoAtTail == nullptr);
            bb->m_numLocals = numLocals;
            bb->m_localInfoAtHead = DfgAlloc()->AllocateArray<PhiOrNode>(numLocals);
            bb->m_localInfoAtTail = DfgAlloc()->AllocateArray<PhiOrNode>(numLocals);
        }
    }

    void AssertBasicBlockHeadTailLocalInfoConsistent(BasicBlock* TESTBUILD_ONLY(bb), bool TESTBUILD_ONLY(isDataFlowInfoSetUp))
    {
#ifdef TESTBUILD
        size_t numLocals = bb->m_numLocals;
        PhiOrNode* localInfoAtHead = bb->m_localInfoAtHead;
        PhiOrNode* localInfoAtTail = bb->m_localInfoAtTail;
        Node* undefVal = m_graph->GetUndefValue().m_node;

        // Validate that for each local, there should only be at most two events:
        // (1) A GetLocal or PhantomLocal or nothing
        // (2) A SetLocal
        //
        // Validate that each GetLocal/SetLocal/PhantomLocal must be either an event head or an event tail
        //
        TempUnorderedSet<Node*> allLocOpNodes(m_alloc);
        for (Node* node : bb->m_nodes)
        {
            if (node->IsGetLocalNode() || node->IsSetLocalNode() || node->IsPhantomLocalNode())
            {
                VirtualRegister vreg = node->GetLocalOperationVirtualRegister();
                TestAssert(vreg.Value() < numLocals);
                TestAssert(!localInfoAtHead[vreg.Value()].IsNull() && !localInfoAtTail[vreg.Value()].IsNull());
                TestAssert(localInfoAtHead[vreg.Value()] == node || localInfoAtTail[vreg.Value()] == node);
                TestAssert(!allLocOpNodes.count(node));
                allLocOpNodes.insert(node);
                if (node->IsGetOrPhantomLocalNode())
                {
                    TestAssertIff(isDataFlowInfoSetUp, node->GetDataFlowInfoForGetLocal() != nullptr);
                }
            }
        }
        // Validate each event head / event tail indeed points to valid nodes in this basic block,
        // and if the head != tail, the head must be a GetLocal/PhantomLocal, and the tail must be a SetLocal
        //
        for (size_t i = 0; i < numLocals; i++)
        {
            TestAssertIff(localInfoAtHead[i].IsNull(), localInfoAtTail[i].IsNull());
            if (!localInfoAtHead[i].IsNull())
            {
                TestAssertIff(localInfoAtHead[i] == undefVal, localInfoAtTail[i] == undefVal);
                if (localInfoAtHead[i] == undefVal)
                {
                    for (BasicBlock* pred : bb->m_predecessors)
                    {
                        TestAssert(pred->m_localInfoAtTail[i] == undefVal);
                    }
                }
                if (localInfoAtHead[i].IsPhi())
                {
                    TestAssert(localInfoAtHead[i] == localInfoAtTail[i]);
                    continue;
                }
                TestAssert(!localInfoAtTail[i].IsPhi());
                TestAssert(allLocOpNodes.count(localInfoAtHead[i].AsNode()));
                TestAssert(allLocOpNodes.count(localInfoAtTail[i].AsNode()));
                if (localInfoAtHead[i].AsNode() != localInfoAtTail[i].AsNode())
                {
                    TestAssert(localInfoAtHead[i].AsNode()->IsGetOrPhantomLocalNode());
                    TestAssert(localInfoAtTail[i].AsNode()->IsSetLocalNode());
                }
            }
        }
#endif
    }

    // Remove redundant or unnecessary GetLocal/SetLocal/PhantomLocal in a basic block.
    // Dead SetLocals are not removed since we need data flow information to do that.
    //
    void CanonicalizeLocalOperationsInBasicBlock(BasicBlock* bb)
    {
        TestAssert(bb->m_numLocals != static_cast<uint32_t>(-1));
        size_t numLocals = bb->m_numLocals;
        TestAssert(numLocals == m_graph->GetTotalNumLocals());

        PhiOrNode* localInfoAtHead = bb->m_localInfoAtHead;
        PhiOrNode* localInfoAtTail = bb->m_localInfoAtTail;
        Node* undefVal = m_graph->GetUndefValue().m_node;
        if (bb == m_graph->GetEntryBB())
        {
            // The function entry block is guaranteed to have all values undef at head,
            // since we require that no block can branch to the entry block
            //
            for (size_t i = 0; i < numLocals; i++)
            {
                localInfoAtHead[i] = undefVal;
            }
            for (size_t i = 0; i < numLocals; i++)
            {
                localInfoAtTail[i] = undefVal;
            }
        }
        else
        {
            for (size_t i = 0; i < numLocals; i++)
            {
                localInfoAtHead[i] = nullptr;
            }
            for (size_t i = 0; i < numLocals; i++)
            {
                localInfoAtTail[i] = nullptr;
            }
        }

        for (Node* node : bb->m_nodes)
        {
            node->DoReplacementForInputsAndSetReferenceBit();
            if (node->IsGetLocalNode())
            {
                VirtualRegister vreg = node->GetLocalOperationVirtualRegister();
                TestAssert(vreg.Value() < numLocals);
                PhiOrNode event = localInfoAtTail[vreg.Value()];
                if (!event.IsNull())
                {
                    switch (event.GetNodeKind())
                    {
                    case NodeKind_GetLocal:
                    {
                        // GetLocal -> GetLocal, this GetLocal can be replaced by the previous GetLocal
                        //
                        node->ConvertToNop();
                        node->SetReplacement(event.AsNode());
                        break;
                    }
                    case NodeKind_SetLocal:
                    {
                        // SetLocal -> GetLocal, this GetLocal can be replaced by the value of the SetLocal
                        //
                        Node* setLocal = event.AsNode();
                        Node* value = setLocal->GetInputEdgeForNodeWithFixedNumInputs<1>(0).GetOperand();
                        node->ConvertToNop();
                        node->SetReplacement(value);
                        break;
                    }
                    case NodeKind_PhantomLocal:
                    {
                        // PhantomLocal -> GetLocal, the earlier PhantomLocal can be removed
                        //
                        Node* phantomLocal = event.AsNode();
                        if (localInfoAtHead[vreg.Value()] == phantomLocal)
                        {
                            localInfoAtHead[vreg.Value()] = node;
                        }
                        phantomLocal->ConvertToNop();
                        node->SetDataFlowInfoForGetLocal(nullptr);
                        localInfoAtTail[vreg.Value()] = node;
                        break;
                    }
                    case NodeKind_UndefValue:
                    {
                        // This local is guaranteed to see UndefValue, it can be replaced by UndefValue
                        //
                        node->ConvertToNop();
                        node->SetReplacement(undefVal);
                        break;
                    }
                    default:
                    {
                        TestAssert(false);
                        __builtin_unreachable();
                    }
                    } /*switch*/
                }
                else
                {
                    node->SetDataFlowInfoForGetLocal(nullptr);
                    TestAssert(localInfoAtHead[vreg.Value()].IsNull());
                    localInfoAtHead[vreg.Value()] = node;
                    localInfoAtTail[vreg.Value()] = node;
                }
            }
            else if (node->IsSetLocalNode())
            {
                VirtualRegister vreg = node->GetLocalOperationVirtualRegister();
                TestAssert(vreg.Value() < numLocals);
                PhiOrNode event = localInfoAtTail[vreg.Value()];
                if (!event.IsNull())
                {
                    switch (event.GetNodeKind())
                    {
                    case NodeKind_GetLocal:
                    case NodeKind_PhantomLocal:
                    {
                        // GetLocal/PhantomLocal -> SetLocal, nothing can be done, just update the tail event
                        //
                        TestAssert(!localInfoAtHead[vreg.Value()].IsNull());
                        localInfoAtTail[vreg.Value()] = node;
                        break;
                    }
                    case NodeKind_UndefValue:
                    {
                        // UndefValue -> SetLocal, update both head and tail event
                        //
                        TestAssert(localInfoAtHead[vreg.Value()] == undefVal);
                        localInfoAtHead[vreg.Value()] = node;
                        localInfoAtTail[vreg.Value()] = node;
                        break;
                    }
                    case NodeKind_SetLocal:
                    {
                        // SetLocal -> SetLocal, the previous SetLocal can be removed
                        //
                        Node* previousSetLocal = event.AsNode();
                        if (localInfoAtHead[vreg.Value()] == previousSetLocal)
                        {
                            localInfoAtHead[vreg.Value()] = node;
                        }
                        previousSetLocal->ConvertToNop();
                        localInfoAtTail[vreg.Value()] = node;
                        break;
                    }
                    default:
                    {
                        TestAssert(false);
                        __builtin_unreachable();
                    }
                    } /*switch*/
                }
                else
                {
                    TestAssert(localInfoAtHead[vreg.Value()].IsNull());
                    localInfoAtHead[vreg.Value()] = node;
                    localInfoAtTail[vreg.Value()] = node;
                }
            }
            else if (node->IsPhantomLocalNode())
            {
                VirtualRegister vreg = node->GetLocalOperationVirtualRegister();
                TestAssert(vreg.Value() < numLocals);
                PhiOrNode event = localInfoAtTail[vreg.Value()];
                if (!event.IsNull())
                {
                    switch (event.GetNodeKind())
                    {
                    case NodeKind_GetLocal:
                    case NodeKind_SetLocal:
                    case NodeKind_PhantomLocal:
                    case NodeKind_UndefValue:
                    {
                        // GetLocal/SetLocal/PhantomLocal/UndefValue -> PhantomLocal, this PhantomLocal can be removed
                        // Note that UndefValue means we are guaranteed that this local hasn't been initialized yet,
                        // so there is no SetLocal that this PhantomLocal can keep alive.
                        //
                        node->ConvertToNop();
                        break;
                    }
                    default:
                    {
                        TestAssert(false);
                        __builtin_unreachable();
                    }
                    }   /*switch*/
                }
                else
                {
                    node->SetDataFlowInfoForGetLocal(nullptr);
                    TestAssert(localInfoAtHead[vreg.Value()].IsNull());
                    localInfoAtHead[vreg.Value()] = node;
                    localInfoAtTail[vreg.Value()] = node;
                }
            }
        }

        // Assert that things are consistent and as expected after canonicalization
        //
        AssertBasicBlockHeadTailLocalInfoConsistent(bb, false /*isDataFlowInfoSetUp*/);
    }

    void CanonicalizeAllLocalOperations()
    {
        m_graph->ClearAllReplacementsAndIsReferencedBit();
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            CanonicalizeLocalOperationsInBasicBlock(bb);
        }
        m_graph->AssertReplacementIsComplete();
    }

    // Assert that the Phi indeed shows up in the expected place in m_localInfoAtHead
    //
    static void AssertPhiValid(Phi* TESTBUILD_ONLY(phi))
    {
#ifdef TESTBUILD
        size_t localOrd = phi->GetLocalOrd();
        BasicBlock* bb = phi->GetBasicBlock();
        TestAssert(localOrd < bb->m_numLocals);
        PhiOrNode valueAtHead = bb->m_localInfoAtHead[localOrd];
        TestAssert(!valueAtHead.IsNull());
        if (valueAtHead.IsPhi())
        {
            TestAssert(phi == valueAtHead);
        }
        else
        {
            TestAssert(valueAtHead.AsNode()->IsGetOrPhantomLocalNode());
            TestAssert(phi == valueAtHead.AsNode()->GetDataFlowInfoForGetLocal());
        }
#endif
    }

    // 'Func' should take two params: Phi* succPhi, size_t incomingOrd
    //
    // This function invokes the functor for each successor of phi, that is, succPhi->IncomingVal(incomingOrd) == phi
    //
    template<typename Func>
    static void ALWAYS_INLINE ForEachPhiSuccessor(Phi* phi, const Func& func)
    {
        AssertPhiValid(phi);
        BasicBlock* bb = phi->GetBasicBlock();
        size_t localOrd = phi->GetLocalOrd();

        TestAssert(!bb->m_localInfoAtTail[localOrd].IsNull());
        if (bb->m_localInfoAtTail[localOrd].GetNodeKind() == NodeKind_SetLocal)
        {
            // This Phi is not the incoming value of any Phi.
            //
            return;
        }
        else
        {
            TestAssert(bb->m_localInfoAtTail[localOrd] == bb->m_localInfoAtHead[localOrd]);
        }

        size_t numSuccessors = bb->GetNumSuccessors();
        for (size_t succOrd = 0; succOrd < numSuccessors; succOrd++)
        {
            BasicBlock* succ = bb->m_successors[succOrd];
            TestAssert(localOrd < succ->m_numLocals);
            PhiOrNode valueAtHead = succ->m_localInfoAtHead[localOrd];
            if (!valueAtHead.IsNull())
            {
                switch (valueAtHead.GetNodeKind())
                {
                case NodeKind_GetLocal:
                case NodeKind_PhantomLocal:
                {
                    Phi* succPhi = valueAtHead.AsNode()->GetDataFlowInfoForGetLocal();
                    size_t incomingOrd = bb->m_predOrdForSuccessors[succOrd];
                    TestAssert(succPhi != nullptr);
                    TestAssert(succPhi->IncomingValue(incomingOrd) == phi);
                    func(succPhi, incomingOrd);
                    break;
                }
                case NodeKind_Phi:
                {
                    Phi* succPhi = valueAtHead.AsPhi();
                    size_t incomingOrd = bb->m_predOrdForSuccessors[succOrd];
                    TestAssert(succPhi != nullptr);
                    TestAssert(succPhi->IncomingValue(incomingOrd) == phi);
                    func(succPhi, incomingOrd);
                    break;
                }
                case NodeKind_SetLocal:
                {
                    break;
                }
                case NodeKind_UndefValue:
                default:
                {
                    TestAssert(false);
                    __builtin_unreachable();
                }
                }
            }
        }
    }

    // It checks the following:
    // (1) Each GetLocal/PhantomLocal node has a valid Phi data flow info
    // (2) Each Phi node's children are valid and agrees with the info in the graph
    // (3) If graph is unified, everything in the same Phi graph has the same LogicalVariable
    // (4) There's no stray Phi nodes: every Phi node should be transitively used by a GetLocal/PhantomLocal
    // (5) If expectNoDeadStores, every SetLocal node should be transitively used by a Phi node
    //
    void AssertPhiGraphConsistent([[maybe_unused]] bool isPreUnification,
                                  [[maybe_unused]] bool expectNoTriviallyUndefPhi,
                                  [[maybe_unused]] bool expectNoDeadStores)
    {
#ifdef TESTBUILD
        auto assertPhiShowsUpAtTail = [](Phi* phi)
        {
            BasicBlock* bb = phi->GetBasicBlock();
            size_t localOrd = phi->GetLocalOrd();
            TestAssert(localOrd < bb->m_numLocals);
            TestAssert(!bb->m_localInfoAtTail[localOrd].IsNull());
            if (bb->m_localInfoAtTail[localOrd].IsPhi())
            {
                TestAssert(phi == bb->m_localInfoAtTail[localOrd].AsPhi());
            }
            else
            {
                TestAssert(bb->m_localInfoAtTail[localOrd].AsNode()->IsGetOrPhantomLocalNode());
                TestAssert(phi == bb->m_localInfoAtTail[localOrd].AsNode()->GetDataFlowInfoForGetLocal());
            }
        };

        TempUnorderedSet<Phi*> allPhis(m_alloc);
        TempUnorderedMap<Node*, bool /*used*/> allStores(m_alloc);

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsGetOrPhantomLocalNode())
                {
                    Phi* phi = node->GetDataFlowInfoForGetLocal();
                    TestAssert(phi != nullptr);
                    TestAssert(!allPhis.count(phi));
                    allPhis.insert(phi);

                    if (!isPreUnification)
                    {
                        TestAssert(node->GetLogicalVariableInfo() == phi->GetLogicalVariable());
                    }
                    else
                    {
                        TestAssert(phi->GetPhiOriginNodeForUnification() == node);
                    }
                }
                if (node->IsSetLocalNode())
                {
                    TestAssert(!allStores.count(node));
                    allStores[node] = false;
                }
            }
            for (size_t i = 0; i < bb->m_numLocals; i++)
            {
                if (!bb->m_localInfoAtHead[i].IsNull() && bb->m_localInfoAtHead[i].IsPhi())
                {
                    Phi* phi = bb->m_localInfoAtHead[i].AsPhi();
                    TestAssert(!allPhis.count(phi));
                    allPhis.insert(phi);
                }
            }
        }

        for (Phi* phi : allPhis)
        {
            if (!isPreUnification)
            {
                TestAssert(phi->GetLogicalVariable() != nullptr);
            }
            AssertPhiValid(phi);

            TestAssertImp(expectNoTriviallyUndefPhi, !phi->IsTriviallyUndefValue());

            bool hasSetLocal = false;
            TestAssert(phi->GetBasicBlock()->m_predecessors.size() == phi->GetNumIncomingValues());
            for (size_t idx = 0; idx < phi->GetNumIncomingValues(); idx++)
            {
                BasicBlock* pred = phi->GetBasicBlock()->m_predecessors[idx];
                PhiOrNode incomingVal = phi->IncomingValue(idx);
                TestAssert(!incomingVal.IsNull());
                NodeKind nodeKind = incomingVal.GetNodeKind();
                TestAssert(nodeKind == NodeKind_Phi || nodeKind == NodeKind_SetLocal || nodeKind == NodeKind_UndefValue);
                if (nodeKind == NodeKind_Phi)
                {
                    Phi* incomingPhi = incomingVal.AsPhi();
                    TestAssert(allPhis.count(incomingPhi));
                    TestAssert(phi->GetLocalOrd() == incomingPhi->GetLocalOrd());
                    TestAssert(pred == incomingPhi->GetBasicBlock());
                    assertPhiShowsUpAtTail(incomingPhi);
                    hasSetLocal |= !incomingVal.AsPhi()->IsTriviallyUndefValue();
                }
                else if (nodeKind == NodeKind_SetLocal)
                {
                    hasSetLocal = true;
                    Node* setLocal = incomingVal.AsNode();
                    TestAssert(setLocal->GetLocalOperationVirtualRegister().Value() == phi->GetLocalOrd());
                    TestAssert(pred->m_localInfoAtTail[phi->GetLocalOrd()] == setLocal);
                    TestAssert(allStores.count(setLocal));
                    allStores[setLocal] = true;
                }
                else
                {
                    TestAssert(nodeKind == NodeKind_UndefValue);
                    PhiOrNode valueAtTail = pred->m_localInfoAtTail[phi->GetLocalOrd()];
                    TestAssert(!valueAtTail.IsNull());
                    TestAssert(valueAtTail.GetNodeKind() == NodeKind_UndefValue);
                }

                if (!isPreUnification)
                {
                    if (nodeKind == NodeKind_Phi)
                    {
                        TestAssert(phi->GetLogicalVariable() == incomingVal.AsPhi()->GetLogicalVariable());
                    }
                    else if (nodeKind == NodeKind_SetLocal)
                    {
                        TestAssert(phi->GetLogicalVariable() == incomingVal.AsNode()->GetLogicalVariableInfo());
                    }
                }
            }
            TestAssertIff(hasSetLocal, !phi->IsTriviallyUndefValue());
        }

        if (expectNoDeadStores)
        {
            for (auto& it : allStores)
            {
                TestAssert(it.second);
            }
        }

        // Assert that there's no stray Phi nodes: every Phi node should be transitively used by a GetLocal/PhantomLocal
        //
        TempVector<Phi*> worklist(m_alloc);
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsGetOrPhantomLocalNode())
                {
                    Phi* phi = node->GetDataFlowInfoForGetLocal();
                    TestAssert(phi != nullptr);
                    TestAssert(allPhis.count(phi));
                    allPhis.erase(allPhis.find(phi));
                    worklist.push_back(phi);
                }
            }
        }
        while (!worklist.empty())
        {
            Phi* phi = worklist.back();
            worklist.pop_back();
            for (size_t idx = 0; idx < phi->GetNumIncomingValues(); idx++)
            {
                PhiOrNode incomingVal = phi->IncomingValue(idx);
                TestAssert(!incomingVal.IsNull());
                if (incomingVal.IsPhi())
                {
                    Phi* incomingPhi = incomingVal.AsPhi();
                    if (allPhis.count(incomingPhi))
                    {
                        allPhis.erase(allPhis.find(incomingPhi));
                        worklist.push_back(incomingPhi);
                    }
                }
            }
        }
        TestAssert(allPhis.empty());
#endif
    }

    // Assert everything we expect at the end of this pass
    //
    void AssertBlockLocalSSAInvariant(bool TESTBUILD_ONLY(isPreUnification))
    {
#ifdef TESTBUILD
        m_graph->AssertReplacementIsComplete();
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            AssertBasicBlockHeadTailLocalInfoConsistent(bb, true /*isDataFlowInfoSetUp*/);
        }
        AssertPhiGraphConsistent(isPreUnification, true /*expectNoTriviallyUndefPhi*/, true /*expectNoDeadStores*/);
#endif
    }

    // Converts GetLocals with no user to PhantomLocal, build Phi data flow graph,
    // remove dead SetLocals, replace GetLocals that are guaranteed to see UndefVal with UndefVal.
    //
    void BuildPhiDataFlowGraph(bool isPreUnification)
    {
        m_graph->FreeMemoryForAllPhi();

        Node* undefVal = m_graph->GetUndefValue().m_node;

        TempVector<Phi*> allPhis(m_alloc);
        TempVector<Phi*> q(m_alloc);

        size_t numLocals = m_graph->GetTotalNumLocals();

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            TestAssert(bb->m_numLocals == numLocals);
            PhiOrNode* localInfoAtHead = bb->m_localInfoAtHead;

            // For each local which head event is GetLocal/PhantomLocal, create a Phi node and push into work queue
            //
            for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
            {
                if (!localInfoAtHead[localOrd].IsNull())
                {
                    Node* node = localInfoAtHead[localOrd].AsNode();
                    if (node->IsGetOrPhantomLocalNode())
                    {
                        // If a GetLocal is not used, convert it to a PhantomLocal.
                        //
                        if (node->IsGetLocalNode() && !node->IsNodeReferenced())
                        {
                            node->m_nodeKind = NodeKind_PhantomLocal;
                        }

                        Phi* phi;
                        if (isPreUnification)
                        {
                            phi = m_graph->AllocatePhi(bb->m_predecessors.size(), bb, localOrd, node);
                        }
                        else
                        {
                            phi = m_graph->AllocatePhi(bb->m_predecessors.size(), bb, localOrd, node->GetLogicalVariableInfo());
                        }
                        allPhis.push_back(phi);

                        TestAssert(node->GetDataFlowInfoForGetLocal() == nullptr);
                        node->SetDataFlowInfoForGetLocal(phi);
                        q.push_back(phi);
                    }
                }
            }

#ifdef TESTBUILD
            // Every GetLocal/PhantomLocal should now have their DataFlowInfo set up
            //
            for (Node* node : bb->m_nodes)
            {
                if (node->IsGetOrPhantomLocalNode())
                {
                    TestAssert(node->GetDataFlowInfoForGetLocal() != nullptr);
                }
            }
#endif
        }

        // This is the work queue for computing whether a Phi is trivially an UndefValue
        //
        TempVector<Phi*> q2(m_alloc);
        bool hasPhiReferenceUndefValue = false;

        while (!q.empty())
        {
            Phi* phi = q.back();
            q.pop_back();

            AssertPhiValid(phi);

            BasicBlock* bb = phi->GetBasicBlock();
            size_t localOrd = phi->GetLocalOrd();

            TestAssert(phi->GetNumIncomingValues() == bb->m_predecessors.size());
            TestAssert(phi->IsTriviallyUndefValue());
            for (size_t predOrd = 0; predOrd < phi->GetNumIncomingValues(); predOrd++)
            {
                TestAssert(phi->IncomingValue(predOrd).IsNull());
                BasicBlock* pred = bb->m_predecessors[predOrd];
                PhiOrNode valueAtTail = pred->m_localInfoAtTail[localOrd];
                if (valueAtTail.IsNull())
                {
                    TestAssert(pred->m_localInfoAtHead[localOrd].IsNull());
                    Phi* incomingPhi;
                    if (isPreUnification)
                    {
                        incomingPhi = m_graph->AllocatePhi(pred->m_predecessors.size(), pred, localOrd, phi->GetPhiOriginNodeForUnification());
                    }
                    else
                    {
                        incomingPhi = m_graph->AllocatePhi(pred->m_predecessors.size(), pred, localOrd, phi->GetLogicalVariable());
                    }
                    allPhis.push_back(incomingPhi);

                    pred->m_localInfoAtHead[localOrd] = incomingPhi;
                    pred->m_localInfoAtTail[localOrd] = incomingPhi;
                    q.push_back(incomingPhi);
                    phi->IncomingValue(predOrd) = incomingPhi;
                }
                else
                {
                    switch (valueAtTail.GetNodeKind())
                    {
                    case NodeKind_GetLocal:
                    case NodeKind_PhantomLocal:
                    {
                        Phi* incomingPhi = valueAtTail.AsNode()->GetDataFlowInfoForGetLocal();
                        TestAssert(incomingPhi != nullptr);
                        phi->IncomingValue(predOrd) = incomingPhi;
                        break;
                    }
                    case NodeKind_SetLocal:
                    {
                        phi->SetNotTriviallyUndefValue();
                        phi->IncomingValue(predOrd) = valueAtTail;
                        break;
                    }
                    case NodeKind_UndefValue:
                    {
                        hasPhiReferenceUndefValue = true;
                        phi->IncomingValue(predOrd) = valueAtTail;
                        break;
                    }
                    case NodeKind_Phi:
                    {
                        phi->IncomingValue(predOrd) = valueAtTail;
                        break;
                    }
                    default:
                    {
                        TestAssert(false);
                        __builtin_unreachable();
                    }
                    }   /*switch*/
                }
                TestAssert(!phi->IncomingValue(predOrd).IsNull());
            }

            if (!phi->IsTriviallyUndefValue())
            {
                q2.push_back(phi);
            }
        }

        // Now, compute whether each Phi is trivially an UndefVal
        //
        if (hasPhiReferenceUndefValue)
        {
            q = std::move(q2);

            while (!q.empty())
            {
                Phi* phi = q.back();
                q.pop_back();

                TestAssert(!phi->IsTriviallyUndefValue());
                ForEachPhiSuccessor(phi, [&](Phi* succPhi, size_t /*incomingOrd*/) ALWAYS_INLINE {
                    if (succPhi->IsTriviallyUndefValue())
                    {
                        succPhi->SetNotTriviallyUndefValue();
                        q.push_back(succPhi);
                    }
                });
            }
        }
        else
        {
            for (Phi* phi : allPhis)
            {
                phi->SetNotTriviallyUndefValue();
            }
        }

        // Sanity check that the Phi graph right now is consistent with the IR graph
        //
        AssertPhiGraphConsistent(isPreUnification, false /*expectNoTriviallyUndefPhi*/, false /*expectNoDeadStores*/);

        // Replace every Phi that is trivially an UndefValue with UndefValue
        //
        if (hasPhiReferenceUndefValue)
        {
            for (Phi* phi : allPhis)
            {
                if (phi->IsTriviallyUndefValue())
                {
                    BasicBlock* bb = phi->GetBasicBlock();
                    size_t localOrd = phi->GetLocalOrd();
                    if (bb->m_localInfoAtHead[localOrd] == phi)
                    {
                        TestAssert(bb->m_localInfoAtTail[localOrd] == phi);
                        bb->m_localInfoAtHead[localOrd] = undefVal;
                        bb->m_localInfoAtTail[localOrd] = undefVal;
                    }
                    ForEachPhiSuccessor(phi, [&](Phi* succPhi, size_t incomingOrd) ALWAYS_INLINE {
                        succPhi->IncomingValue(incomingOrd) = undefVal;
                    });
                }
            }
        }

        // Remove dead SetLocals
        // Turn GetLocal that will trivially see UndefValue (i.e., no SetLocal ever flows into this GetLocal) into UndefValue
        //
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            bool hasReplacement = false;
            for (Node* node : bb->m_nodes)
            {
                if (hasReplacement)
                {
                    node->DoReplacementForInputs();
                }
                if (node->IsGetOrPhantomLocalNode())
                {
                    if (node->GetDataFlowInfoForGetLocal()->IsTriviallyUndefValue())
                    {
                        size_t localOrd = node->GetLocalOperationVirtualRegister().Value();
                        TestAssert(bb->m_localInfoAtHead[localOrd] == node);
                        if (bb->m_localInfoAtTail[localOrd] == node)
                        {
                            bb->m_localInfoAtHead[localOrd] = undefVal;
                            bb->m_localInfoAtTail[localOrd] = undefVal;
                        }
                        else
                        {
                            TestAssert(!bb->m_localInfoAtTail[localOrd].IsNull());
                            TestAssert(bb->m_localInfoAtTail[localOrd].GetNodeKind() == NodeKind_SetLocal);
                            bb->m_localInfoAtHead[localOrd] = bb->m_localInfoAtTail[localOrd];
                        }
                        if (node->IsGetLocalNode())
                        {
                            node->ConvertToNop();
                            node->SetReplacement(undefVal);
                            hasReplacement = true;
                        }
                        else
                        {
                            TestAssert(node->IsPhantomLocalNode());
                            node->ConvertToNop();
                        }
                    }
                }
                else if (node->IsSetLocalNode())
                {
                    size_t localOrd = node->GetLocalOperationVirtualRegister().Value();
                    TestAssert(localOrd < numLocals);
                    TestAssert(bb->m_localInfoAtTail[localOrd] == node);
                    size_t numSuccessors = bb->GetNumSuccessors();
                    bool hasUsers = false;
                    for (size_t succOrd = 0; succOrd < numSuccessors; succOrd++)
                    {
                        BasicBlock* succ = bb->m_successors[succOrd];
                        PhiOrNode valueAtHead = succ->m_localInfoAtHead[localOrd];
                        if (!valueAtHead.IsNull())
                        {
                            if (valueAtHead.IsPhi())
                            {
                                TestAssert(valueAtHead.AsPhi()->IncomingValue(bb->m_predOrdForSuccessors[succOrd]) == node);
                                hasUsers = true;
                                break;
                            }
                            else if (valueAtHead.AsNode()->IsGetOrPhantomLocalNode())
                            {
                                TestAssert(valueAtHead.AsNode()->GetDataFlowInfoForGetLocal()->IncomingValue(bb->m_predOrdForSuccessors[succOrd]) == node);
                                hasUsers = true;
                                break;
                            }
                        }
                    }
                    if (!hasUsers)
                    {
                        if (bb->m_localInfoAtHead[localOrd] == node)
                        {
                            bb->m_localInfoAtHead[localOrd] = nullptr;
                            bb->m_localInfoAtTail[localOrd] = nullptr;
                        }
                        else
                        {
                            bb->m_localInfoAtTail[localOrd] = bb->m_localInfoAtHead[localOrd];
                        }
                        node->ConvertToNop();
                    }
                }
            }
        }
    }

    // Set up Logical Variable for each GetLocal/SetLocal/PhantomLocal
    //
    // The invariant here is that for any SetLocal that can flow to a GetLocal/PhantomLocal,
    // the SetLocal and the GetLocal/PhantomLocal must have the same logical variable.
    //
    void SetupLogicalVariables()
    {
        size_t numLocals = m_graph->GetTotalNumLocals();

        TempVector<Phi*> allPhis(m_alloc);
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            PhiOrNode* localInfoAtHead = bb->m_localInfoAtHead;
            for (size_t localOrd = 0; localOrd < numLocals; localOrd++)
            {
                PhiOrNode valueAtHead = localInfoAtHead[localOrd];
                if (!valueAtHead.IsNull())
                {
                    if (valueAtHead.IsPhi())
                    {
                        allPhis.push_back(valueAtHead.AsPhi());
                    }
                    else if (valueAtHead.AsNode()->IsGetOrPhantomLocalNode())
                    {
                        allPhis.push_back(valueAtHead.AsNode()->GetDataFlowInfoForGetLocal());
                    }
                }
            }
        }

        for (Phi* phi : allPhis)
        {
            TestAssert(phi != nullptr);
            size_t numIncomingValues = phi->GetNumIncomingValues();
            Node* node1 = phi->GetPhiOriginNodeForUnification();
            for (size_t idx = 0; idx < numIncomingValues; idx++)
            {
                PhiOrNode incomingValue = phi->IncomingValue(idx);
                if (incomingValue.IsPhi())
                {
                    Node* node2 = incomingValue.AsPhi()->GetPhiOriginNodeForUnification();
                    node1->MergeLogicalVariableInfo(node2);
                }
                else if (incomingValue.AsNode()->IsSetLocalNode())
                {
                    Node* node2 = incomingValue.AsNode();
                    node1->MergeLogicalVariableInfo(node2);
                }
                else
                {
                    TestAssert(incomingValue.AsNode()->IsUndefValueNode());
                }
            }
        }

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                if (node->IsGetLocalNode() || node->IsSetLocalNode() || node->IsPhantomLocalNode())
                {
                    node->SetupLogicalVariableInfoAfterDsuMerge(m_graph);
                }
            }
        }

        for (Phi* phi : allPhis)
        {
            LogicalVariableInfo* info = phi->GetPhiOriginNodeForUnification()->GetLogicalVariableInfo();
            phi->SetLogicalVariable(info);
        }
    }

    void RunPass()
    {
        TestAssert(m_graph->IsCfgAvailable());
        TestAssert(m_graph->IsPreUnificationForm() || m_graph->IsLoadStoreForm());
        bool isPreUnification = m_graph->IsPreUnificationForm();
        if (isPreUnification)
        {
            AllocateStateArraysForLocals();
        }
        CanonicalizeAllLocalOperations();
        BuildPhiDataFlowGraph(isPreUnification);
        AssertBlockLocalSSAInvariant(isPreUnification);
        if (isPreUnification)
        {
            SetupLogicalVariables();
            AssertBlockLocalSSAInvariant(false /*isPreUnification*/);
        }
        m_graph->UpgradeToBlockLocalSSAForm();
    }
};

void InitializeBlockLocalSSAFormAndSetupLogicalVariables(Graph* graph)
{
    TestAssert(graph->IsPreUnificationForm());
    ConstructBlockLocalSSAPass pass(graph);
    pass.RunPass();
    TestAssert(graph->IsBlockLocalSSAForm());
}

void ReconstructBlockLocalSSAFormFromLoadStoreForm(Graph* graph)
{
    TestAssert(graph->IsLoadStoreForm());
    ConstructBlockLocalSSAPass pass(graph);
    pass.RunPass();
    TestAssert(graph->IsBlockLocalSSAForm());
}

}  // namespace dfg
