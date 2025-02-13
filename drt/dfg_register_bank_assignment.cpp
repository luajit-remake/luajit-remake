#include "common_utils.h"
#include "dfg_node.h"
#include "dfg_variant_trait_table.h"
#include "temp_arena_allocator.h"
#include "bytecode_builder.h"

namespace dfg {

struct RegBankAvailabilityInfo
{
    RegBankAvailabilityInfo() : m_epoch(0) { }

    enum class State : uint8_t
    {
        // We know we'll be fine to load this SSA value into either GPR or FPR, but we haven't decided yet
        // At this state, there may be a list of Edges that we need to update once we make the decision
        //
        Undecided = 0,
        // This SSA value is available in FPR
        //
        InFPR = 1,
        // This SSA value is available in GPR
        //
        InGPR = 2,
        // This SSA value is available in both GPR and FPR
        //
        InGPRAndFPR = 3
    };

    struct PendingChoiceList
    {
        PendingChoiceList(Node* node, PendingChoiceList* next)
        {
            TestAssert(reinterpret_cast<uint64_t>(node) % 2 == 0);
            m_compositeValue = reinterpret_cast<uint64_t>(node) | 1;
            m_next = next;
        }

        PendingChoiceList(Edge* edge, PendingChoiceList* next)
        {
            TestAssert(reinterpret_cast<uint64_t>(edge) % 2 == 0);
            m_compositeValue = reinterpret_cast<uint64_t>(edge);
            m_next = next;
        }

        void Patch(bool shouldPutInGPR)
        {
            if (m_compositeValue & 1)
            {
                Node* node = reinterpret_cast<Node*>(m_compositeValue ^ 1);
                node->SetOutputRegisterBankDecision(shouldPutInGPR);
            }
            else
            {
                Edge* edge = reinterpret_cast<Edge*>(m_compositeValue);
                edge->SetShouldUseGPR(shouldPutInGPR);
            }
        }

        // If lowest bit == 0, this is an Edge*, otherwise this is a Node* and we should update its output value
        //
        uint64_t m_compositeValue;
        PendingChoiceList* m_next;
    };

    void ProcessUndecidedOutput(TempArenaAllocator& alloc, TempVector<RegBankAvailabilityInfo*>& list /*inout*/, uint32_t epoch, Node* node, bool prefersGPR)
    {
        TestAssert(epoch != m_epoch);
        m_epoch = epoch;
        m_state = State::Undecided;
        m_preferenceCount = (prefersGPR ? 1 : -1);
        m_pendingChoices = alloc.AllocateObject<PendingChoiceList>(node, nullptr /*next*/);
        list.push_back(this);
    }

    void ProcessUndecidedUse(TempArenaAllocator& alloc, TempVector<RegBankAvailabilityInfo*>& list /*inout*/, uint32_t epoch, Edge* edge, bool prefersGPR)
    {
        if (epoch != m_epoch)
        {
            m_epoch = epoch;
            m_state = State::Undecided;
            m_preferenceCount = (prefersGPR ? 1 : -1);
            m_pendingChoices = alloc.AllocateObject<PendingChoiceList>(edge, nullptr /*next*/);
            list.push_back(this);
            return;
        }

        if (m_state == State::Undecided)
        {
            m_preferenceCount += (prefersGPR ? 1 : -1);
            m_pendingChoices = alloc.AllocateObject<PendingChoiceList>(edge, m_pendingChoices);
        }
        else
        {
            bool shouldUseGPR = (m_state == State::InGPR) || (m_state == State::InGPRAndFPR && prefersGPR);
            edge->SetShouldUseGPR(shouldUseGPR);
        }
    }

    // If 'mustUseGPR' is true, the value must be available in GPR to fulfill this node
    // Otherwise, the value must be available in FPR to fulfill this node
    //
    void ProcessMandetoryRegBankChoice(uint32_t epoch, bool mustUseGPR)
    {
        if (epoch != m_epoch)
        {
            m_epoch = epoch;
            m_state = (mustUseGPR ? State::InGPR : State::InFPR);
            return;
        }

        if (m_state == State::Undecided)
        {
            m_state = (mustUseGPR ? State::InGPR : State::InFPR);
            UpdateAllPendingChoices(mustUseGPR);
        }
        else
        {
            m_state = static_cast<State>(static_cast<uint8_t>(m_state) | static_cast<uint8_t>(mustUseGPR ? State::InGPR : State::InFPR));
        }
    }

    void Finalize([[maybe_unused]] uint32_t epoch)
    {
        TestAssert(m_epoch == epoch);
        if (m_state == State::Undecided)
        {
            // All uses of this SSA value is fine with either GPR or FPR. Use preference count to decide.
            //
            bool shouldUseGpr = (m_preferenceCount >= 0);
            UpdateAllPendingChoices(shouldUseGpr);
        }
    }

private:
    void UpdateAllPendingChoices(bool shouldUseGPR)
    {
        PendingChoiceList* curEdge = m_pendingChoices;
        while (curEdge != nullptr)
        {
            curEdge->Patch(shouldUseGPR);
            curEdge = curEdge->m_next;
        }
    }

    // When the current epoch is different from m_epoch, the state is logically Undecided
    //
    uint32_t m_epoch;
    State m_state;
    // When State is Undecided, a positive value means more nodes prefer GPR
    //
    int16_t m_preferenceCount;
    // The list of edges that we must update once we transition away from Undecided state
    //
    PendingChoiceList* m_pendingChoices;
};

namespace {

struct RegisterBankAssignmentPass
{
    static void Run(Graph* graph)
    {
        RegisterBankAssignmentPass pass;
        pass.RunImpl(graph);
    }

private:
    RegisterBankAssignmentPass()
        : m_tempAlloc()
        , m_bbAlloc()
        , m_allPendingInfo(m_tempAlloc)
        , m_epoch(1)
    { }

    void FinishUpAndAdvanceToNextEpoch()
    {
        // TODO: the "preferGpr" hint is a very weak hint.
        // It would be better if we could decide GPR/FPR based on current GPR/FPR pressure.
        //
        for (RegBankAvailabilityInfo* info : m_allPendingInfo)
        {
            info->Finalize(m_epoch);
        }
        m_allPendingInfo.clear();
        m_epoch++;
    }

    RegBankAvailabilityInfo* GetInfoData(Node* node, uint16_t outputOrd)
    {
        TestAssert(node->IsOutputOrdValid(outputOrd));
        RegBankAvailabilityInfo* infoData = node->GetRegBankAvailabilityInfoArray();
        return infoData + outputOrd;
    }

    RegBankAvailabilityInfo* GetInfoData(Edge& e)
    {
        return GetInfoData(e.GetOperand(), e.GetOutputOrdinal());
    }

    RegBankAvailabilityInfo* GetInfoDataForDirectOutput(Node* node)
    {
        TestAssert(node->HasDirectOutput());
        return GetInfoData(node, 0 /*outputOrd*/);
    }

    void ProcessDirectOutputWithMandetoryRegBank(Node* node, bool mustUseGpr)
    {
        node->SetOutputRegisterBankDecision(mustUseGpr);
        RegBankAvailabilityInfo* info = GetInfoDataForDirectOutput(node);
        info->ProcessMandetoryRegBankChoice(m_epoch, mustUseGpr);
    }

    // Note that the reg bank for type check is always known from the check, and have no choice to be made
    // The 'shouldUseGPR' bit in the edge always reflect the reg bank decision for the node, not the type check
    //
    void ProcessTypeCheck(Edge& edge, bool mustUseGpr)
    {
        RegBankAvailabilityInfo* info = GetInfoData(edge);
        info->ProcessMandetoryRegBankChoice(m_epoch, mustUseGpr);
    }

    void ProcessUseWithMandetoryRegBank(Edge& edge, bool mustUseGpr)
    {
        edge.SetShouldUseGPR(mustUseGpr);
        RegBankAvailabilityInfo* info = GetInfoData(edge);
        info->ProcessMandetoryRegBankChoice(m_epoch, mustUseGpr);
    }

    void ProcessUndecidedDirectOutput(Node* node, bool prefersGpr)
    {
        RegBankAvailabilityInfo* info = GetInfoDataForDirectOutput(node);
        info->ProcessUndecidedOutput(m_bbAlloc, m_allPendingInfo /*inout*/, m_epoch, node, prefersGpr);
    }

    void ProcessUndecidedUse(Edge& edge, bool prefersGpr)
    {
        RegBankAvailabilityInfo* info = GetInfoData(edge);
        info->ProcessUndecidedUse(m_bbAlloc, m_allPendingInfo /*inout*/, m_epoch, &edge, prefersGpr);
    }

    void ProcessBasicBlock(BasicBlock* bb)
    {
        m_bbAlloc.Reset();
        TestAssert(m_allPendingInfo.empty());

        for (Node* node : bb->m_nodes)
        {
            // Allocate RegBankAvailabilityInfo for each output
            //
            bool hasDirectOutput = node->HasDirectOutput();
            size_t numExtraOutputs = node->GetNumExtraOutputs();
            size_t numTotalOutputs = numExtraOutputs + (hasDirectOutput ? 1 : 0);
            if (numTotalOutputs > 0)
            {
                RegBankAvailabilityInfo* infoData = m_bbAlloc.AllocateArray<RegBankAvailabilityInfo>(numTotalOutputs);
                if (!hasDirectOutput)
                {
                    infoData--;
                }
                node->SetRegBankAvailabilityInfoArray(infoData);
            }

            // For each edge that requires a check, take into account the register bank requirement for this check
            //
            node->ForEachInputEdge(
                [&](Edge& edge) ALWAYS_INLINE
                {
                    if (edge.NeedsTypeCheck())
                    {
                        bool mustUseGpr = ShouldTypeCheckOperandUseGPR(edge.GetUseKind());
                        ProcessTypeCheck(edge, mustUseGpr);
                    }
                });

            if (node->IsBuiltinNodeKind())
            {
                NodeKind nodeKind = node->GetNodeKind();
                switch (nodeKind)
                {
                case NodeKind_Constant:
                case NodeKind_UnboxedConstant:
                case NodeKind_UndefValue:
                case NodeKind_Argument:
                case NodeKind_GetNumVariadicArgs:
                case NodeKind_GetKthVariadicArg:
                case NodeKind_GetFunctionObject:
                case NodeKind_Phi:
                case NodeKind_FirstAvailableGuestLanguageNodeKind:
                {
                    TestAssert(false && "unexpected node kind in basic block!");
                    __builtin_unreachable();
                }
                case NodeKind_GetLocal:
                case NodeKind_SetLocal:
                case NodeKind_CreateCapturedVar:
                case NodeKind_GetCapturedVar:
                case NodeKind_SetCapturedVar:
                case NodeKind_GetKthVariadicRes:
                case NodeKind_GetNumVariadicRes:
                case NodeKind_CheckU64InBound:
                case NodeKind_I64SubSaturateToZero:
                case NodeKind_GetUpvalueImmutable:
                case NodeKind_GetUpvalueMutable:
                case NodeKind_SetUpvalue:
                {
                    // These nodes have reg alloc enabled, have fixed number of inputs/outputs,
                    // and is directly handled by one DfgVariantTraits
                    //
                    const DfgVariantTraits* info = GetCodegenInfoForBuiltinNodeKind(nodeKind);
                    TestAssert(info != nullptr);
                    TestAssert(info->IsRegAllocEnabled());
                    TestAssertIff(info->HasOutput(), node->HasDirectOutput());
                    TestAssert(!node->HasExtraOutput());
                    if (info->HasOutput())
                    {
                        DfgNodeOperandRegBankPref pref = info->Output();
                        TestAssert(pref.Valid());
                        TestAssert(pref.GprAllowed() || pref.FprAllowed());
                        if (pref.HasChoices())
                        {
                            ProcessUndecidedDirectOutput(node, pref.GprPreferred());
                        }
                        else
                        {
                            ProcessDirectOutputWithMandetoryRegBank(node, pref.GprAllowed() /*mustUseGpr*/);
                        }
                    }
                    size_t numOperands = info->NumOperandsForRA();
                    TestAssert(numOperands == node->GetNumInputs());
                    TestAssert(numOperands <= 2);
                    __builtin_assume(numOperands <= 2);
                    for (uint32_t operandIdx = 0; operandIdx < numOperands; operandIdx++)
                    {
                        Edge& e = node->GetInputEdge(operandIdx);
                        DfgNodeOperandRegBankPref pref = info->Operand(operandIdx);
                        TestAssert(pref.Valid());
                        TestAssert(pref.GprAllowed() || pref.FprAllowed());
                        if (pref.HasChoices())
                        {
                            ProcessUndecidedUse(e, pref.GprPreferred());
                        }
                        else
                        {
                            ProcessUseWithMandetoryRegBank(e, pref.GprAllowed() /*mustUseGpr*/);
                        }
                    }
                    break;
                }
                case NodeKind_Nop:
                {
                    // No output, and all inputs should have checks, nothing to take care of now
                    //
                    break;
                }
                case NodeKind_ShadowStore:
                case NodeKind_ShadowStoreUndefToRange:
                case NodeKind_Phantom:
                {
                    // We specially handle these nodes in codegen
                    //
                    break;
                }
                case NodeKind_CreateVariadicRes:
                {
                    bool isFirst = true;
                    node->ForEachInputEdge(
                        [&](Edge& e) ALWAYS_INLINE
                        {
                            if (isFirst)
                            {
                                isFirst = false;
                                ProcessUseWithMandetoryRegBank(e, true /*mustUseGpr*/);
                            }
                            else
                            {
                                ProcessUndecidedUse(e, true /*prefersGpr*/);
                            }
                        });
                    TestAssert(!isFirst);
                    break;
                }
                case NodeKind_PrependVariadicRes:
                {
                    node->ForEachInputEdge(
                        [&](Edge& e) ALWAYS_INLINE
                        {
                            ProcessUndecidedUse(e, true /*prefersGpr*/);
                        });
                    break;
                }
                case NodeKind_CreateFunctionObject:
                {
                    // This node is implemented by multiple subcomponents.
                    // The first subcomponent (happens after the checks) disables reg alloc
                    //
                    FinishUpAndAdvanceToNextEpoch();
                    // All of its inputs are on the stack at the time they are used.
                    // However, they cannot be used on the stack: we still need to load then into either a GPR or a FPR to use them,
                    // and they may stay in that register afterwards.
                    //
                    {
                        size_t curInputOrd = 0;
                        node->ForEachInputEdge(
                            [&](Edge& e) ALWAYS_INLINE
                            {
                                if (curInputOrd < 2)
                                {
                                    // Input 0: parent function object
                                    // Input 1: UnlinkedCodeBlock
                                    // both needs to be GPR
                                    //
                                    ProcessUseWithMandetoryRegBank(e, true /*mustUseGpr*/);
                                }
                                else
                                {
                                    // The rest of the inputs are captured values, both GPR and FPR are fine
                                    //
                                    ProcessUndecidedUse(e, true /*prefersGpr*/);
                                }
                                curInputOrd++;
                            });
                        TestAssert(curInputOrd == node->GetNumInputs());
                    }
                    // The output is in a GPR register
                    //
                    ProcessDirectOutputWithMandetoryRegBank(node, true /*mustUseGpr*/);
                    break;
                }
                case NodeKind_Return:
                {
                    // All of the inputs may be used when they are in register (both GPR and FPR are OK)
                    //
                    node->ForEachInputEdge(
                        [&](Edge& e) ALWAYS_INLINE
                        {
                            ProcessUndecidedUse(e, true /*prefersGpr*/);
                        });
                    // But the final return logic happens with reg alloc disabled
                    //
                    FinishUpAndAdvanceToNextEpoch();
                    break;
                }
                }   /*switch*/
            }
            else
            {
                // For guest language nodes, the codegen workflow is that:
                // 1. Store the range operands to the stack (the range operands may be from either GPR or FPR)
                // 2. Execute all the checks
                // 3. Execute the main node (if the node disables reg alloc, everything is spilled at this moment)
                //
                BCKind bcKind = node->GetGuestLanguageBCKind();
                TestAssert(bcKind < BCKind::X_END_OF_ENUM);

                TestAssert(node->GetNodeSpecializedForInliningKind() == Node::SISKind::None && "speculative inlining variants not handled yet");
                uint8_t numFixedSSAOperands = DeegenBytecodeBuilder::BytecodeDecoder::BytecodeNumFixedSSAOperands(bcKind);
                uint32_t numInputs = node->GetNumInputs();
                TestAssert(numInputs >= numFixedSSAOperands);
                TestAssertImp(!DeegenBytecodeBuilder::BytecodeDecoder::BytecodeHasRangeOperand(bcKind), numInputs == numFixedSSAOperands);

                for (uint32_t i = numFixedSSAOperands; i < numInputs; i++)
                {
                    ProcessUndecidedUse(node->GetInputEdge(i), true /*prefersGpr*/);
                }

                const DfgVariantTraits* info = GetCodegenInfoForDfgVariant(bcKind, node->DfgVariantOrd());
                TestAssert(info != nullptr);

                if (info->IsRegAllocEnabled())
                {
                    TestAssertIff(info->HasOutput(), node->HasDirectOutput());
                    if (info->HasOutput())
                    {
                        DfgNodeOperandRegBankPref pref = info->Output();
                        TestAssert(pref.Valid());
                        TestAssert(pref.GprAllowed() || pref.FprAllowed());
                        if (pref.HasChoices())
                        {
                            ProcessUndecidedDirectOutput(node, pref.GprPreferred());
                        }
                        else
                        {
                            ProcessDirectOutputWithMandetoryRegBank(node, pref.GprAllowed() /*mustUseGpr*/);
                        }
                    }
                    TestAssert(numFixedSSAOperands == info->NumOperandsForRA());
                    for (uint32_t operandIdx = 0; operandIdx < numFixedSSAOperands; operandIdx++)
                    {
                        Edge& e = node->GetInputEdge(operandIdx);
                        DfgNodeOperandRegBankPref pref = info->Operand(operandIdx);
                        TestAssert(pref.Valid());
                        TestAssert(pref.GprAllowed() || pref.FprAllowed());
                        if (pref.HasChoices())
                        {
                            ProcessUndecidedUse(e, pref.GprPreferred());
                        }
                        else
                        {
                            ProcessUseWithMandetoryRegBank(e, pref.GprAllowed() /*mustUseGpr*/);
                        }
                    }
                }
                else
                {
                    // Reg alloc is disabled, the operands are used when they are on the stack.
                    // No need to make them in register when the node is executed,
                    // so we should not call ProcessUndecidedUse before FinishUpAndAdvanceToNextEpoch.
                    // And they cannot survive in a register after node execution either,
                    // so we should not call ProcessUndecidedUse after FinishUpAndAdvanceToNextEpoch either.
                    // (note how this is different from CreateFunctionObject where the operands are used
                    // while on the stack but *can* survive in a register after use).
                    //
                    FinishUpAndAdvanceToNextEpoch();
                }
            }
        }
        FinishUpAndAdvanceToNextEpoch();

        // Validate post-condition
        // This is not a full validation: it only validates that the register selection is allowed by the variant.
        // Furthermore, for simplicity, it does not validate this for built-in nodes with complex codegen.
        //
#ifdef TESTBUILD
        for (Node* node : bb->m_nodes)
        {
            const DfgVariantTraits* info = nullptr;
            if (node->IsBuiltinNodeKind())
            {
                info = GetCodegenInfoForBuiltinNodeKind(node->GetNodeKind());
                if (info == nullptr)
                {
                    // This is a built-in node with complex codegen, skip validation for simplicity
                    //
                    continue;
                }
                TestAssert(info->IsRegAllocEnabled());
                TestAssert(node->GetNumInputs() == info->NumOperandsForRA());
            }
            else
            {
                BCKind bcKind = node->GetGuestLanguageBCKind();
                TestAssert(bcKind < BCKind::X_END_OF_ENUM);
                info = GetCodegenInfoForDfgVariant(bcKind, node->DfgVariantOrd());
                if (!info->IsRegAllocEnabled())
                {
                    continue;
                }
            }

            TestAssert(info != nullptr);
            TestAssert(info->IsRegAllocEnabled());
            TestAssertIff(info->HasOutput(), node->HasDirectOutput());
            if (info->HasOutput())
            {
                if (node->ShouldOutputRegisterBankUseGPR())
                {
                    TestAssert(info->Output().GprAllowed());
                }
                else
                {
                    TestAssert(info->Output().FprAllowed());
                }
            }
            TestAssert(node->GetNumInputs() >= info->NumOperandsForRA());
            for (uint32_t i = 0; i < info->NumOperandsForRA(); i++)
            {
                if (node->GetInputEdge(i).ShouldUseGPR())
                {
                    TestAssert(info->Operand(i).GprAllowed());
                }
                else
                {
                    TestAssert(info->Operand(i).FprAllowed());
                }
            }
        }
#endif
    }

    void RunImpl(Graph* graph)
    {
        graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                RegBankAvailabilityInfo* infoData = m_tempAlloc.AllocateObject<RegBankAvailabilityInfo>();
                node->SetRegBankAvailabilityInfoArray(infoData);
            });

        for (BasicBlock* bb : graph->m_blocks)
        {
            ProcessBasicBlock(bb);
        }
        TestAssert(m_allPendingInfo.empty());
    }

    TempArenaAllocator m_tempAlloc;
    // This allocator is used to allocate things that gets freed at each new basic block
    //
    TempArenaAllocator m_bbAlloc;
    TempVector<RegBankAvailabilityInfo*> m_allPendingInfo;
    uint32_t m_epoch;
};

}   // anonymous namespace

void RunRegisterBankAssignmentPass(Graph* graph)
{
    RegisterBankAssignmentPass::Run(graph);
}

}   // namespace dfg
