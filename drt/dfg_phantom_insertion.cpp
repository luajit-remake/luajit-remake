#include "dfg_phantom_insertion.h"
#include "dfg_ir_dump.h"

namespace dfg {

namespace {

struct PhantomInsertionPassImpl
{
    TempArenaAllocator m_alloc;
    Graph* m_graph;

    // A non-null value represents that this SSA value is currently stored in the imaginary interpreter slot,
    // and that this SSA value hasn't been stored to the corresponding DFG slot by a SetLocal
    //
    TempVector<Value> m_interpSlotVal;
    // A bitvector recording whether each interpreter slot has a non-null value, always agrees with m_interpSlotVal
    //
    TempBitVector m_interpSlotHasValue;
#ifdef TESTBUILD
    // If an index is true, it denotes that the interpreter slot value has been saved to the DFG slot by a SetLocal
    // For assertion purpose only
    //
    TempBitVector m_interpSlotSavedToDfgSlot;
#endif

    BatchedInsertions m_inserts;
    BasicBlock* m_currentBasicBlock;

    // All nodes between the (k-1)-th node that may exit (inclusive) and the k-th (1-indexed) node that may exit (exclusive)
    // are considered to have exit ordinal k.
    // That is, everything from BB start to the first node that may exit (exclusive) are considered to have exit ordinal 1,
    // everything from the first node that may exit (inclusive) to the second node that may exit (exclusive) have exit ordinal 2, etc..
    //
    uint32_t m_lastExitOrd;
    OsrExitDestination m_lastExitDest;
    size_t m_nodeIndexOfLastExitDest;

    // All SSA values in the basic block are given a unique index into this array.
    // m_lastUseExitOrd[i] denotes the exit ordinal of the last node that uses SSA value i.
    //
    TempVector<uint32_t> m_lastUseExitOrd;
    size_t m_totalSSAValuesInBB;

    PhantomInsertionPassImpl(Graph* graph)
        : m_alloc()
        , m_graph(graph)
        , m_interpSlotVal(m_alloc)
        , m_inserts(m_alloc)
        , m_lastUseExitOrd(m_alloc)
    {
        size_t numInterpreterSlots = m_graph->GetTotalNumInterpreterSlots();
        m_interpSlotVal.resize(numInterpreterSlots);
        m_interpSlotHasValue.Reset(m_alloc, numInterpreterSlots);
#ifdef TESTBUILD
        m_interpSlotSavedToDfgSlot.Reset(m_alloc, numInterpreterSlots);
#endif
    }

    void ProcessInterpreterSlotDeath(size_t interpSlotOrd)
    {
        TestAssert(interpSlotOrd < m_interpSlotVal.size());
        TestAssert(!m_interpSlotVal[interpSlotOrd].IsNull());
        TestAssert(m_interpSlotHasValue.IsSet(interpSlotOrd));

        Value value = m_interpSlotVal[interpSlotOrd];
        if (!value.GetOperand()->IsConstantLikeNode())
        {
            size_t ssaOrd = value.GetOperand()->GetCustomMarker() + value.m_outputOrd;
            TestAssert(ssaOrd < m_totalSSAValuesInBB);

            TestAssert(m_lastUseExitOrd[ssaOrd] != 0);
            // See ProcessBasicBlock's comment near m_lastUseExitOrd update logic for explanation
            //
            bool needPhantom = (m_lastUseExitOrd[ssaOrd] < m_lastExitOrd);

            if (needPhantom)
            {
                // We should insert a Phantom after m_lastExitDest
                //
                TestAssert(m_nodeIndexOfLastExitDest != static_cast<size_t>(-1));
                TestAssert(m_nodeIndexOfLastExitDest < m_currentBasicBlock->m_nodes.size());
                Node* exitingNode = m_currentBasicBlock->m_nodes[m_nodeIndexOfLastExitDest];
                Node* phantom = Node::CreatePhantomNode(value);
                phantom->SetNodeOrigin(exitingNode->GetNodeOrigin());
                phantom->SetOsrExitDest(exitingNode->GetOsrExitDest());
                phantom->SetExitOK(false);
                m_inserts.Add(m_nodeIndexOfLastExitDest + 1 /*insertBefore*/, phantom);
            }
        }

        m_interpSlotVal[interpSlotOrd] = nullptr;
        m_interpSlotHasValue.ClearBit(interpSlotOrd);
    }

    void ProcessBasicBlock(BasicBlock* bb)
    {
        m_currentBasicBlock = bb;
        memset(m_interpSlotVal.data(), 0, sizeof(Value) * m_interpSlotVal.size());

        m_interpSlotHasValue.Clear();
#ifdef TESTBUILD
        m_interpSlotSavedToDfgSlot.Clear();
#endif
        m_inserts.Reset(bb);

        m_lastExitOrd = 1;
        bool seenBranchDest = false;
        m_lastExitDest = OsrExitDestination();
        TestAssert(m_lastExitDest.IsInvalid());
        m_nodeIndexOfLastExitDest = static_cast<size_t>(-1);

        DVector<ArenaPtr<Node>>& nodes = bb->m_nodes;

        size_t numSSAValues = 0;
        for (Node* node : nodes)
        {
            node->SetCustomMarker(numSSAValues);
            // For simplicity, reserve a slot for direct output even if the node doesn't have one
            //
            numSSAValues += 1 + node->GetNumExtraOutputs();
        }
        m_totalSSAValuesInBB = numSSAValues;

        if (numSSAValues > m_lastUseExitOrd.size())
        {
            m_lastUseExitOrd.resize(numSSAValues);
        }

        for (size_t i = 0; i < numSSAValues; i++)
        {
            m_lastUseExitOrd[i] = 0;
        }

        auto handleOsrExitDest = [&](OsrExitDestination newDest) ALWAYS_INLINE
        {
            TestAssert(!newDest.IsInvalid());
            if (newDest.IsBranchDest())
            {
                TestAssertImp(seenBranchDest, newDest == m_lastExitDest);
                if (seenBranchDest)
                {
                    return;
                }
                seenBranchDest = true;

                // This must be the last OSR exit destination in this basic block (the IR validator validates this)
                // The interpreter slots that are live are those slots that are live at tail
                //
                m_interpSlotHasValue.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
                    if (!m_currentBasicBlock->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(ord)).IsLive())
                    {
                        ProcessInterpreterSlotDeath(ord);
                    }
                });

                return;
            }

#ifdef TESTBUILD
            // All interpreter slots saved by SetLocals must be live at this point
            //
            {
                CodeOriginLivenessInfo livenessInfo(newDest.GetNormalDestination());
                m_interpSlotSavedToDfgSlot.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
                    TestAssert(livenessInfo.IsLive(InterpreterSlot(ord)));
                });
            }
#endif

            TestAssert(!seenBranchDest);
            if (m_lastExitDest.IsInvalid())
            {
                CodeOriginLivenessInfo livenessInfo(newDest.GetNormalDestination());

                // This is the first time we hit a valid exit location in this BB
                // Simply go through all interpreter slots to kill dead slots
                //
                m_interpSlotHasValue.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
                    if (!livenessInfo.IsLive(InterpreterSlot(ord)))
                    {
                        ProcessInterpreterSlotDeath(ord);
                    }
                });

                return;
            }

            // Now we know we have seen a valid exit destination before, we only need to check the interpreter slots that was live in the old
            // exit destination but dead in the current exit destination
            //
            TestAssert(!m_lastExitDest.IsInvalid());
            TestAssert(!m_lastExitDest.IsBranchDest());
            TestAssert(!newDest.IsBranchDest());

            if (newDest == m_lastExitDest)
            {
                return;
            }

            CodeOrigin oldCodeOrigin = m_lastExitDest.GetNormalDestination();
            CodeOrigin newCodeOrigin = newDest.GetNormalDestination();

            InlinedCallFrame* oldFrame = oldCodeOrigin.GetInlinedCallFrame();
            InlinedCallFrame* newFrame = newCodeOrigin.GetInlinedCallFrame();

            if (oldFrame == newFrame)
            {
                // The common easy case: everything before the frame is unchanged,
                // we only need to figure out the bytecode locals that are live at the old exit but dead right now
                //
                const BitVectorView oldLiveness = oldFrame->BytecodeLivenessInfo().GetLiveness(oldCodeOrigin.GetBytecodeIndex(), BytecodeLiveness::BeforeUse);
                const BitVectorView newLiveness = oldFrame->BytecodeLivenessInfo().GetLiveness(newCodeOrigin.GetBytecodeIndex(), BytecodeLiveness::BeforeUse);
                size_t frameBase = oldFrame->GetInterpreterSlotForStackFrameBase().Value();

#ifdef TESTBUILD
                // For each value that is dead in the old exit but live in the new exit,
                // there must be a ShadowStore that assigns a value to it.
                //
                newLiveness.ForEachSetBitThatIsClearInOther(oldLiveness /*other*/, [&](size_t bytecodeLocalOrd) ALWAYS_INLINE {
                    // Assert that there must be a ShadowStore, but we also need to consider the case that
                    // a SetLocal has stored the value to the DFG slot, in which case m_interpSlotHasValue will be false
                    // but m_interpSlotSavedToDfgSlot will be true
                    //
                    size_t interpSlotOrd = frameBase + bytecodeLocalOrd;
                    TestAssert(m_interpSlotHasValue.IsSet(interpSlotOrd) || m_interpSlotSavedToDfgSlot.IsSet(interpSlotOrd));
                });
#endif

                // For each value that is live in the old exit but dead in the new exit, handle death
                //
                oldLiveness.ForEachSetBitThatIsClearInOther(newLiveness /*other*/, [&](size_t bytecodeLocalOrd) ALWAYS_INLINE {
                    size_t interpSlotOrd = frameBase + bytecodeLocalOrd;
                    TestAssert(interpSlotOrd < m_interpSlotVal.size());
                    TestAssertIff(!m_interpSlotVal[interpSlotOrd].IsNull(), m_interpSlotHasValue.IsSet(interpSlotOrd));
                    if (!m_interpSlotVal[interpSlotOrd].IsNull())
                    {
                        ProcessInterpreterSlotDeath(interpSlotOrd);
                    }
                });
            }
            else
            {
                // We must do a full comparison of the live state
                //
                CodeOriginLivenessInfo oldLivenessInfo(oldCodeOrigin);
                CodeOriginLivenessInfo newLivenessInfo(newCodeOrigin);

#ifdef TESTBUILD
                // For each value that is dead in the old exit but live in the new exit,
                // there must be a ShadowStore that assigns a value to it.
                //
                for (size_t ord = 0; ord < m_interpSlotVal.size(); ord++)
                {
                    if (!oldLivenessInfo.IsLive(InterpreterSlot(ord)) && newLivenessInfo.IsLive(InterpreterSlot(ord)))
                    {
                        TestAssert(m_interpSlotHasValue.IsSet(ord) || m_interpSlotSavedToDfgSlot.IsSet(ord));
                    }
                }
#endif

                m_interpSlotHasValue.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
                    bool isLiveInOld = oldLivenessInfo.IsLive(InterpreterSlot(ord));
                    bool isLiveInNew = newLivenessInfo.IsLive(InterpreterSlot(ord));
                    if (isLiveInOld && !isLiveInNew)
                    {
                        ProcessInterpreterSlotDeath(ord);
                    }
                });
            }
        };

        for (size_t nodeIdx = 0, numNodes = nodes.size(); nodeIdx < numNodes; nodeIdx++)
        {
            Node* node = nodes[nodeIdx];
            bool nodeMayExit = node->MayOsrExit();

            // Update last use for all SSA values used by this node and all SSA values defined by this node
            // If this node may exit, we need to use the bumped lastExitOrd, but the lastExitOrd itself should not be updated yet.
            //
            // After this operation, the invariant here is that if an interpreter slot dies at this point,
            // we need to insert a Phantom if the exit ordinal of its last use is < m_lastExitOrd (which means,
            // the last explicit use of this SSA value is by a node that strictly precedes the last node that may exit.
            // That is, the last node that may exit is beyond the explicit live range of the SSA value, so we need
            // to insert a Phantom after that node to extend the live range)
            //
            {
                uint32_t exitOrd = nodeMayExit ? (m_lastExitOrd + 1) : m_lastExitOrd;
                node->ForEachInputEdge([&](Edge& e) ALWAYS_INLINE {
                    Node* inputNode = e.GetOperand();
                    if (inputNode->IsConstantLikeNode()) { return; }
                    size_t ssaOrd = inputNode->GetCustomMarker() + e.GetOutputOrdinal();
                    TestAssert(ssaOrd < numSSAValues);
                    m_lastUseExitOrd[ssaOrd] = exitOrd;
                });

                for (size_t outputOrd = (node->HasDirectOutput() ? 0 : 1), numExtraOutputs = node->GetNumExtraOutputs();
                     outputOrd <= numExtraOutputs;
                     outputOrd++)
                {
                    size_t ssaOrd = node->GetCustomMarker() + outputOrd;
                    TestAssert(ssaOrd < numSSAValues);
                    m_lastUseExitOrd[ssaOrd] = exitOrd;
                }
            }

            auto processShadowStore = [&](InterpreterSlot slot, Value value) ALWAYS_INLINE
            {
                TestAssert(slot.Value() < m_interpSlotVal.size());
                // It is impossible that a SetLocal to this slot has occured: SetLocal should
                // always be the last thing that happened to a slot in a BB
                //
                TestAssert(!m_interpSlotSavedToDfgSlot.IsSet(slot.Value()));
                if (!m_interpSlotVal[slot.Value()].IsNull())
                {
                    // The ShadowStore just overwrote the old value, the old value is dead now
                    //
                    ProcessInterpreterSlotDeath(slot.Value());
                }
                m_interpSlotVal[slot.Value()] = value;
                m_interpSlotHasValue.SetBit(slot.Value());
            };

            if (node->IsShadowStoreNode())
            {
                InterpreterSlot slot = node->GetShadowStoreInterpreterSlotOrd();
                Value value = node->GetSoleInput().GetValue();
                processShadowStore(slot, value);
            }
            else if (node->IsShadowStoreUndefToRangeNode())
            {
                Value undefVal = m_graph->GetUndefValue();
                InterpreterSlot slotStart = node->GetShadowStoreUndefToRangeStartInterpSlotOrd();
                size_t numSlots = node->GetShadowStoreUndefToRangeRangeLength();
                for (size_t i = 0; i < numSlots; i++)
                {
                    processShadowStore(InterpreterSlot(slotStart.Value() + i), undefVal);
                }
            }
            else if (node->IsSetLocalNode())
            {
                uint32_t slot = node->GetLogicalVariable()->m_interpreterSlotOrd;
                // Two SetLocals to the same slot must not happen as block-local SSA should have canonicalized it
                //
                TestAssert(!m_interpSlotSavedToDfgSlot.IsSet(slot));
                // A ShadowStore must have happened to the slot and is still live: if the interpreter slot is already
                // dead (which causes the m_interpSlotVal to be cleared), the SetLocal would be unnecessary
                //
                TestAssert(!m_interpSlotVal[slot].IsNull());
                TestAssert(m_interpSlotHasValue.IsSet(slot));
                // The SSA value currently stored in the interpreter slot must equal the value stored by the SetLocal
                //
                TestAssert(node->GetSoleInput().GetValue().IsIdenticalAs(m_interpSlotVal[slot]));
                // The SSA value is stored into the DFG slot, so it is available until the end of BB. No Phantoms needed.
                //
                m_interpSlotVal[slot] = nullptr;
                m_interpSlotHasValue.ClearBit(slot);
#ifdef TESTBUILD
                m_interpSlotSavedToDfgSlot.SetBit(slot);
#endif
            }

            if (nodeMayExit)
            {
                handleOsrExitDest(node->GetOsrExitDest());
                m_lastExitOrd++;
                m_lastExitDest = node->GetOsrExitDest();
                m_nodeIndexOfLastExitDest = nodeIdx;
            }
        }

#ifdef TESTBUILD
        // Assert that the records are consistent with each other
        //
        for (size_t slotOrd = 0; slotOrd < m_interpSlotVal.size(); slotOrd++)
        {
            TestAssertIff(m_interpSlotVal[slotOrd].IsNull(), !m_interpSlotHasValue.IsSet(slotOrd));
            TestAssertImp(m_interpSlotSavedToDfgSlot.IsSet(slotOrd), !m_interpSlotHasValue.IsSet(slotOrd));
        }
        // Assert that all SSA values that's not stored into a local must be dead at the end of BB,
        // which is required for correctness
        //
        for (size_t slotOrd = 0; slotOrd < m_interpSlotVal.size(); slotOrd++)
        {
            if (!m_interpSlotVal[slotOrd].IsNull())
            {
                VirtualRegisterMappingInfo vrmi = bb->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(slotOrd));
                if (vrmi.IsLive())
                {
                    TestAssert(vrmi.IsUmmapedToAnyVirtualReg());
                    TestAssert(m_interpSlotVal[slotOrd].GetOperand()->IsConstantLikeNode());
                }
            }
        }
        // All SetLocals must be live at the end of BB, as otherwise it should have been eliminated by block-local SSA construction pass
        // This assertion might no longer hold once we have optimizations that sinks operations across BB
        //
        m_interpSlotSavedToDfgSlot.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
            TestAssert(bb->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(ord)).IsLive());
        });
#endif

        // All the SSA values that are currently live dies at the end of the BB
        // Process the death event for each of them
        //
        m_interpSlotHasValue.ForEachSetBit([&](size_t ord) ALWAYS_INLINE {
            ProcessInterpreterSlotDeath(ord);
        });

        // Commit all the phantom nodes inserted
        //
        m_inserts.Commit();

        // Assert that the expected invariant holds: the DFG live range of every SSA value always cover all OSR exits
        // that needs this SSA value.
        //
        AssertConsistency();
    }

    void AssertConsistency()
    {
#ifdef TESTBUILD
        for (size_t i = 0; i < m_totalSSAValuesInBB; i++)
        {
            m_lastUseExitOrd[i] = static_cast<uint32_t>(-1);
        }

        auto getSSAValueOrd = [&](Value val)
        {
            Node* operand = val.GetOperand();
            TestAssert(!operand->IsPhantomNode());
            TestAssert(!operand->IsConstantLikeNode());
            size_t ssaOrd = operand->GetCustomMarker() + val.m_outputOrd;
            TestAssert(ssaOrd < m_totalSSAValuesInBB);
            return ssaOrd;
        };

        auto updateUse = [&](Value val, size_t nodeIdx)
        {
            m_lastUseExitOrd[getSSAValueOrd(val)] = SafeIntegerCast<uint32_t>(nodeIdx);
        };

        for (size_t nodeIdx = 0; nodeIdx < m_currentBasicBlock->m_nodes.size(); nodeIdx++)
        {
            Node* node = m_currentBasicBlock->m_nodes[nodeIdx];
            if (node->IsPhantomNode())
            {
                Value val = node->GetSoleInput().GetValue();
                TestAssert(!val.GetOperand()->IsConstantLikeNode());
                updateUse(val, nodeIdx);
                continue;
            }

            node->ForEachInputEdge([&](Edge& e) {
                if (e.GetOperand()->IsConstantLikeNode()) { return; }
                updateUse(e.GetValue(), nodeIdx);
            });

            for (size_t outputOrd = (node->HasDirectOutput() ? 0 : 1); outputOrd <= node->GetNumExtraOutputs(); outputOrd++)
            {
                size_t ssaOrd = node->GetCustomMarker() + outputOrd;
                TestAssert(ssaOrd < m_totalSSAValuesInBB);
                m_lastUseExitOrd[ssaOrd] = SafeIntegerCast<uint32_t>(nodeIdx);
            }
        }

        for (size_t i = 0; i < m_interpSlotVal.size(); i++)
        {
            m_interpSlotVal[i] = nullptr;
        }

        for (size_t nodeIdx = 0; nodeIdx < m_currentBasicBlock->m_nodes.size(); nodeIdx++)
        {
            Node* node = m_currentBasicBlock->m_nodes[nodeIdx];
            if (node->MayOsrExit())
            {
                OsrExitDestination exitDest = node->GetOsrExitDest();
                if (exitDest.IsBranchDest())
                {
                    for (size_t slot = 0; slot < m_interpSlotVal.size(); slot++)
                    {
                        if (!m_interpSlotVal[slot].IsNull() && !m_interpSlotVal[slot].GetOperand()->IsConstantLikeNode())
                        {
                            if (m_currentBasicBlock->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(slot)).IsLive())
                            {
                                size_t ssaOrd = getSSAValueOrd(m_interpSlotVal[slot]);
                                TestAssert(m_lastUseExitOrd[ssaOrd] != static_cast<uint32_t>(-1));
                                TestAssert(m_lastUseExitOrd[ssaOrd] >= nodeIdx);
                            }
                        }
                    }
                }
                else
                {
                    CodeOrigin codeOrigin = exitDest.GetNormalDestination();
                    CodeOriginLivenessInfo livenessInfo(codeOrigin);
                    for (size_t slot = 0; slot < m_interpSlotVal.size(); slot++)
                    {
                        if (!m_interpSlotVal[slot].IsNull() && !m_interpSlotVal[slot].GetOperand()->IsConstantLikeNode())
                        {
                            if (livenessInfo.IsLive(InterpreterSlot(slot)))
                            {
                                size_t ssaOrd = getSSAValueOrd(m_interpSlotVal[slot]);
                                TestAssert(m_lastUseExitOrd[ssaOrd] != static_cast<uint32_t>(-1));
                                TestAssert(m_lastUseExitOrd[ssaOrd] >= nodeIdx);
                            }
                        }
                    }
                }
            }

            if (node->IsShadowStoreNode())
            {
                InterpreterSlot slot = node->GetShadowStoreInterpreterSlotOrd();
                TestAssert(slot.Value() < m_interpSlotVal.size());
                m_interpSlotVal[slot.Value()] = node->GetSoleInput().GetValue();
            }
            else if (node->IsShadowStoreUndefToRangeNode())
            {
                InterpreterSlot slotStart = node->GetShadowStoreUndefToRangeStartInterpSlotOrd();
                size_t numSlots = node->GetShadowStoreUndefToRangeRangeLength();
                for (size_t i = 0; i < numSlots; i++)
                {
                    size_t slotOrd = slotStart.Value() + i;
                    TestAssert(slotOrd < m_interpSlotVal.size());
                    m_interpSlotVal[slotOrd] = m_graph->GetUndefValue();
                }
            }
            else if (node->IsSetLocalNode())
            {
                uint32_t slot = node->GetLogicalVariable()->m_interpreterSlotOrd;
                TestAssert(slot < m_interpSlotVal.size());
                m_interpSlotVal[slot] = nullptr;
            }
        }

        for (size_t slot = 0; slot < m_interpSlotVal.size(); slot++)
        {
            if (!m_interpSlotVal[slot].IsNull())
            {
                VirtualRegisterMappingInfo vrmi = m_currentBasicBlock->GetVirtualRegisterForInterpreterSlotAtTail(InterpreterSlot(slot));
                if (vrmi.IsLive())
                {
                    TestAssert(vrmi.IsUmmapedToAnyVirtualReg());
                    TestAssert(m_interpSlotVal[slot].GetOperand()->IsConstantLikeNode());
                }
            }
        }
#endif
    }

    void Run()
    {
        for (BasicBlock* bb : m_graph->m_blocks)
        {
            ProcessBasicBlock(bb);
        }
    }
};

}   // anonymous namespace

void RunPhantomInsertionPass(Graph* graph)
{
    PhantomInsertionPassImpl pass(graph);
    pass.Run();
}

}   // namespace dfg
