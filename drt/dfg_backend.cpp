#include "common_utils.h"
#include "dfg_backend.h"
#include "temp_arena_allocator.h"
#include "dfg_node.h"
#include "dfg_codegen_operation_log.h"
#include "dfg_reg_alloc_value_manager.h"
#include "dfg_reg_alloc_decision_maker.h"
#include "dfg_variant_trait_table.h"
#include "dfg_test_branch_inst_generator.h"
#include "x64_multi_byte_nop_instruction.h"
#include "jit_function_entry_codegen_helper.h"

namespace dfg {

namespace {

// Utility class to build the use list of each SSA value
//
struct ValueUseListBuilder
{
    // 'bbAlloc' is used to hold information that can be freed after processing each BB
    //
    ValueUseListBuilder(TempArenaAllocator& passAlloc, TempArenaAllocator& bbAlloc, Graph* graph)
        : m_passAlloc(passAlloc)
        , m_bbAlloc(bbAlloc)
        , m_graph(graph)
        , m_nodeInfo(m_passAlloc)
        , m_nextSpillAllUseIndexList(m_passAlloc)
        , m_operandsNeedTypeCheck(m_passAlloc)
    { }

    void SetupUseInfoForAllConstants()
    {
        m_graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                uint16_t ident;
                NodeKind nodeKind = node->GetNodeKind();
                switch (nodeKind)
                {
                case NodeKind_Constant:
                case NodeKind_UnboxedConstant:
                {
                    int64_t ordInConstantTable = node->GetOrdInConstantTable();
                    TestAssert(ordInConstantTable < 0);
                    ident = static_cast<uint16_t>(static_cast<uint64_t>(ordInConstantTable));
                    TestAssert(ident > 0x8003 + 255);
                    break;
                }
                case NodeKind_UndefValue:
                {
                    ident = 0x8000;
                    break;
                }
                case NodeKind_Argument:
                {
                    ident = SafeIntegerCast<uint16_t>(node->GetArgumentOrdinal());
                    break;
                }
                case NodeKind_GetNumVariadicArgs:
                {
                    ident = 0x8001;
                    break;
                }
                case NodeKind_GetKthVariadicArg:
                {
                    uint64_t varArgOrd = node->GetNodeSpecificDataAsUInt64();
                    TestAssert(varArgOrd <= 255);
                    ident = static_cast<uint16_t>(0x8003 + varArgOrd);
                    break;
                }
                case NodeKind_GetFunctionObject:
                {
                    ident = 0x8002;
                    break;
                }
                default:
                {
                    TestAssert(false && "not a constant-like node!");
                    __builtin_unreachable();
                }
                }   /*switch*/

                ValueRegAllocInfo* info = DfgAlloc()->AllocateObject<ValueRegAllocInfo>(node, ident);
                node->SetValueRegAllocInfo(info);
            });
    }

    static ValueRegAllocInfo* GetValueRegAllocInfo(Edge& e)
    {
        Node* node = e.GetOperand();
        TestAssert(node->IsOutputOrdValid(e.GetOutputOrdinal()));
        return node->GetValueRegAllocInfo() + e.GetOutputOrdinal();
    }

    void AssertValueRegAllocInfoForAllConstantLikeNodeInResettedState()
    {
       // This is tricky but needed for performance: due to how this class is used (we build the use list for one
       // basic block then immediately do reg alloc for that basic block, consuming the use list in the process),
       // at the time this function is called, the ValueRegAllocInfo for all the constants should have been
       // resetted to their initial state, so we do not need to manually reset them. Assert this.
       //
#ifdef TESTBUILD
        m_graph->ForEachConstantLikeNode(
            [&](Node* node) ALWAYS_INLINE
            {
                TestAssert(node->IsConstantLikeNode());
                ValueRegAllocInfo* info = node->GetValueRegAllocInfo();
                TestAssert(info->IsConstantLikeNode());
                TestAssert(info->GetConstantLikeNode() == node);
                TestAssert(!info->IsAvailableInGPR());
                TestAssert(!info->IsAvailableInFPR());
                TestAssert(!info->GetGprNextUseInfo().HasNextUse());
                TestAssert(!info->GetFprNextUseInfo().HasNextUse());
            });
#endif
    }

    // Results are populated into the public members of this class
    //
    void ProcessBasicBlock(BasicBlock* bb)
    {
        AssertValueRegAllocInfoForAllConstantLikeNodeInResettedState();

        m_brDecision = nullptr;
        m_brDecisionUse = nullptr;

        auto& nodes = bb->m_nodes;

        // Allocate ValueRegAllocInfo for each SSA value
        //
        for (Node* node : nodes)
        {
            bool hasDirectOutput = node->HasDirectOutput();
            size_t numExtraOutput = node->GetNumExtraOutputs();
            size_t numTotalOutputs = numExtraOutput + (hasDirectOutput ? 1 : 0);
            if (numTotalOutputs > 0)
            {
                ValueRegAllocInfo* info = DfgAlloc()->AllocateArray<ValueRegAllocInfo>(numTotalOutputs);
                if (!hasDirectOutput)
                {
                    info--;
                }
                node->SetValueRegAllocInfo(info);
            }
        }

        uint32_t useIndex = static_cast<uint32_t>(nodes.size() * 3);
        TestAssert(useIndex + 1 < ValueNextUseInfo::x_noNextUse);

        TestAssert(bb->GetNumSuccessors() <= 2);
        if (bb->GetNumSuccessors() > 1)
        {
            // Initialize the brDecision SSA value and its use (one single GPR use at end of block, so with highest useIndex)
            //
            m_brDecision = DfgAlloc()->AllocateObject<ValueRegAllocInfo>();
            m_brDecisionUse = m_bbAlloc.AllocateObject<ValueUseRAInfo>();
            m_brDecisionUse->Initialize(m_brDecision, useIndex + 1 /*useIndex*/, true /*isGprUse*/, false /*isGhostLikeUse*/, true /*doNotProduceDuplicateEdge*/);

            // For sanity, assert that there should be exactly one branchy node
            //
#ifdef TESTBUILD
            bool found = false;
            for (Node* node : nodes)
            {
                if (node->GetNumNodeControlFlowSuccessors() == 2)
                {
                    TestAssert(node == bb->GetTerminator());
                    TestAssert(!found);
                    found = true;
                }
            }
            TestAssert(found);
#endif
        }
        else
        {
#ifdef TESTBUILD
            for (Node* node : nodes)
            {
                TestAssert(node->GetNumNodeControlFlowSuccessors() <= 1);
            }
#endif
        }

        m_nodeInfo.clear();
        ResizeVectorTo(m_nodeInfo, nodes.size(), nullptr /*value*/);
        TestAssert(m_nodeInfo.size() == nodes.size());

        m_nextSpillAllUseIndexList.clear();
        // Sentry value. Note that the brDecision use happens at useIndex + 1.. So the first non-existent index is useIndex + 2
        //
        m_nextSpillAllUseIndexList.push_back(useIndex + 2);

        // Populate the use list of each SSA value
        //
        for (size_t curNodeIndex = nodes.size(); curNodeIndex--;)
        {
            Node* node = nodes[curNodeIndex];
            // Figure out how many checks this node have
            //
            m_operandsNeedTypeCheck.clear();
            node->ForEachInputEdge(
                [&](Edge& e) ALWAYS_INLINE
                {
                    if (e.NeedsTypeCheck())
                    {
                        m_operandsNeedTypeCheck.push_back(&e);
                    }
                });

            uint16_t numChecks = SafeIntegerCast<uint16_t>(m_operandsNeedTypeCheck.size());
            uint16_t numFixedOperands;

            NodeKind nodeKind = node->GetNodeKind();
            if (node->IsBuiltinNodeKind())
            {
                // For built-in nodes, all operands are treated as fixed operands, and there are no ranged operands
                // This also means that we always execute the type checks first before using any operands
                //
                numFixedOperands = SafeIntegerCast<uint16_t>(node->GetNumInputs());
                if (nodeKind == NodeKind_Nop)
                {
                    numFixedOperands = 0;
                }
                // There are two builtin nodes that requires spilling everything:
                // CreateFunctionObject and Return
                //
                if (nodeKind == NodeKind_CreateFunctionObject)
                {
                    // For CreateFunctionObject, the spill happens at the *start* of the main phase (which use index is the value of useIndex now):
                    // all operands are used after being spilled, so if NextUse >= useIndex, it will be spilled before use
                    //
                    NoteSpillEverythingUseIndex(useIndex);
                }
                else if (nodeKind == NodeKind_Return)
                {
                    // For Return, the spill happens at the *end* of the main phase (useIndex) after using the operands,
                    // so if NextUse == useIndex, it can still be used before spilled
                    //
                    NoteSpillEverythingUseIndex(useIndex + 1);
                }
            }
            else
            {
                BCKind bcKind = node->GetGuestLanguageBCKind();
                TestAssert(bcKind < BCKind::X_END_OF_ENUM);

                TestAssert(node->GetNodeSpecializedForInliningKind() == Node::SISKind::None && "speculative inlining variants not handled yet");
                numFixedOperands = DeegenBytecodeBuilder::BytecodeDecoder::BytecodeNumFixedSSAOperands(bcKind);

                const DfgVariantTraits* info = GetCodegenInfoForDfgVariant(bcKind, node->DfgVariantOrd());
                TestAssert(info != nullptr);
                if (!info->IsRegAllocEnabled())
                {
                    // Everything will be spilled at the start of the main phase (use index == useIndex)
                    //
                    NoteSpillEverythingUseIndex(useIndex);
                }
            }

            TestAssert(numFixedOperands <= node->GetNumInputs());

            uint16_t numRangedOperands = SafeIntegerCast<uint16_t>(node->GetNumInputs() - numFixedOperands);
            if (nodeKind == NodeKind_Nop)
            {
                numRangedOperands = 0;
            }

            bool isGhostLikeNode = (nodeKind == NodeKind_ShadowStore ||
                                    nodeKind == NodeKind_ShadowStoreUndefToRange ||
                                    nodeKind == NodeKind_Phantom);

            if (nodeKind == NodeKind_ShadowStore)
            {
                TestAssert(numFixedOperands == 1 && numRangedOperands == 0 && numChecks == 0);

                // We should only treat the ShadowStore operand as a use if it is the last use of the SSA value
                // DEVNOTE:
                //     In fact, if ShadowStore is the last use of an SSA value, this ShadowStore should have no observable effect,
                //     since after phantom insertion, the last use of an SSA value is also exactly when we can free this SSA value,
                //     and since a ShadowStore cannot cause an OSR exit itself, what happens here is we update the shadow slot,
                //     but then the shadow slot immediately dies, so nobody can see this update.
                //     TODO: So I believe it is correct to change this ShadowStore to Phantom in this case (note that we still must keep
                //     the value alive until this point, so it's wrong to simply delete the ShadowStore), but I don't think it's worth
                //     the complexity to do so now.
                //
                ValueRegAllocInfo* info = GetValueRegAllocInfo(node->GetSoleInput());
                bool isLastUse = !info->GetGprNextUseInfo().HasNextUse() && !info->GetFprNextUseInfo().HasNextUse();
                if (!isLastUse)
                {
                    // Skip the only operand
                    // We normally can't directly modify numFixedOperands since we assume rangedOperands follow fixedOperands,
                    // but it's fine here since we have no rangedOperands here
                    //
                    numFixedOperands = 0;
                }
            }

#ifdef TESTBUILD
            // Phantom node must be the last use, assert this
            //
            if (nodeKind == NodeKind_Phantom)
            {
                ValueRegAllocInfo* info = GetValueRegAllocInfo(node->GetSoleInput());
                bool isLastUse = !info->GetGprNextUseInfo().HasNextUse() && !info->GetFprNextUseInfo().HasNextUse();
                TestAssert(isLastUse);
            }
#endif

            // Set up use data for this node
            //
            NodeRegAllocInfo* info = NodeRegAllocInfo::Create(m_bbAlloc, numFixedOperands, numRangedOperands, numChecks);
            m_nodeInfo[curNodeIndex] = info;

            info->m_isGhostLikeNode = isGhostLikeNode;
            info->m_isShadowStoreNode = (nodeKind == NodeKind_ShadowStore);

            // Must initialize in reverse order of use: fixedOperands first, check second, rangedOperands last
            //
            // Initialize FixedOperand uses
            //
            {
                std::span<ValueUseRAInfo> fixedOperandUses = info->GetFixedOperands();
                TestAssert(fixedOperandUses.size() == numFixedOperands);
                for (uint32_t i = 0; i < numFixedOperands; i++)
                {
                    Edge& e = node->GetInputEdge(i);
                    ValueUseRAInfo* use = fixedOperandUses.data() + i;
                    ConstructInPlace(use);
                    use->Initialize(GetValueRegAllocInfo(e), useIndex, e.ShouldUseGPR(), isGhostLikeNode, false /*doNotProduceDuplicateEdge*/);
                }
                TestAssert(useIndex > 0);
                useIndex--;
            }

            // Initialize Check uses
            //
            {
                std::span<ValueUseRAInfo> checkUses = info->GetCheckOperands();
                TestAssert(checkUses.size() == numChecks);
                TestAssert(m_operandsNeedTypeCheck.size() == numChecks);
                // We are using the operands one by one despite that they are having the same useIndex
                // So the uses must be initialized in reversed order (and we use them in the forward order),
                // and no duplicate edge even if multiple checks are on the same SSA value
                //
                for (uint32_t i = numChecks; i--;)
                {
                    // ShadowStore/Phantom edges should not have checks
                    //
                    TestAssert(!isGhostLikeNode);
                    Edge& e = *m_operandsNeedTypeCheck[i];
                    TestAssert(e.NeedsTypeCheck());
                    bool shouldUseGpr = ShouldTypeCheckOperandUseGPR(e.GetUseKind());
                    ValueUseRAInfo* use = checkUses.data() + i;
                    ConstructInPlace(use);
                    use->Initialize(GetValueRegAllocInfo(e), useIndex, shouldUseGpr, false /*isGhostLikeNode*/, true /*doNotProduceDuplicateEdge*/);
                }
                TestAssert(useIndex > 0);
                useIndex--;
            }

            // Initialize RangedOperand uses
            //
            {
                std::span<ValueUseRAInfo> rangedOperandUses = info->GetRangedOperands();
                TestAssert(rangedOperandUses.size() == numRangedOperands);
                TestAssert(numFixedOperands + numRangedOperands <= node->GetNumInputs());
                for (uint32_t i = 0; i < numRangedOperands; i++)
                {
                    // ShadowStore/Phantom edges should not have ranged operands
                    //
                    TestAssert(!isGhostLikeNode);
                    Edge& e = node->GetInputEdge(i + numFixedOperands);
                    ValueUseRAInfo* use = rangedOperandUses.data() + i;
                    ConstructInPlace(use);
                    use->Initialize(GetValueRegAllocInfo(e), useIndex, e.ShouldUseGPR(), false /*isGhostLikeNode*/, false /*doNotProduceDuplicateEdge*/);
                }
                TestAssert(useIndex > 0);
                useIndex--;
            }
        }
        TestAssert(useIndex == 0);
    }

private:
    void NoteSpillEverythingUseIndex(uint32_t useIndex)
    {
        TestAssert(!m_nextSpillAllUseIndexList.empty());
        TestAssert(useIndex <= m_nextSpillAllUseIndexList.back());
        if (useIndex < m_nextSpillAllUseIndexList.back())
        {
            m_nextSpillAllUseIndexList.push_back(useIndex);
        }
    }

    TempArenaAllocator& m_passAlloc;
    TempArenaAllocator& m_bbAlloc;

public:
    Graph* m_graph;
    // Populated by ProcessBasicBlock(), the usage info for each node
    //
    TempVector<NodeRegAllocInfo*> m_nodeInfo;
    // The SSA value for brDecision, if it exists
    //
    ValueRegAllocInfo* m_brDecision;
    // The "use" of the brDecision at basic block end
    //
    ValueUseRAInfo* m_brDecisionUse;
    // The list of all useIndex where everything is spilled, in *descending* order
    // If the next use of a value is >= NextSpillEverythingIndex, it will be spilled before next use
    //
    TempVector<uint32_t> m_nextSpillAllUseIndexList;

private:
    TempVector<Edge*> m_operandsNeedTypeCheck;
};

struct BasicBlockCodegenInfo;

struct BasicBlockTerminatorCodegenInfo
{
    enum class Kind : uint8_t
    {
        // A conditional branch
        //
        CondBr,
        // An unconditional branch
        //
        UncondBr,
        // No successor, should append ud2 at end
        //
        NoSuccessor,
        X_END_OF_ENUM
    };

    BasicBlockTerminatorCodegenInfo()
        : m_kind(Kind::X_END_OF_ENUM)
        , m_isTestValueInRegInitialized(false)
        , m_isTestValueInReg(false)
        , m_isTestForNonZero(false)
        , m_isDefaultTargetFallthrough(false)
        , m_testReg(X64Reg::RSP)
        , m_testPhysicalSlot(static_cast<uint16_t>(-1))
        , m_defaultTargetOrd(static_cast<uint32_t>(-1))
        , m_branchTargetOrd(static_cast<uint32_t>(-1))
    { }

    void InitUnconditionalBranch(uint32_t selfOrd, uint32_t destOrd)
    {
        TestAssert(m_kind == Kind::X_END_OF_ENUM);
        m_kind = Kind::UncondBr;
        m_isDefaultTargetFallthrough = (selfOrd + 1 == destOrd);
        m_defaultTargetOrd = destOrd;
    }

    void SetConditionValueAsReg(X64Reg testReg)
    {
        // It's possible that m_kind is UncondBr if it was a conditional branch but both targets are equal, so it gets converted to UncondBr.
        // In which case the condition value has no use, so doesn't matter. Same for SetConditionValueAsStackSlot
        //
        TestAssert(m_kind == Kind::CondBr || m_kind == Kind::UncondBr);
        TestAssert(testReg.IsGPR());
        TestAssert(!m_isTestValueInRegInitialized);
        m_isTestValueInRegInitialized = true;
        m_isTestValueInReg = true;
        m_testReg = testReg;
    }

    void SetConditionValueAsStackSlot(uint16_t physicalSlotOrd)
    {
        TestAssert(m_kind == Kind::CondBr || m_kind == Kind::UncondBr);
        TestAssert(!m_isTestValueInRegInitialized);
        m_isTestValueInRegInitialized = true;
        m_isTestValueInReg = false;
        m_testPhysicalSlot = physicalSlotOrd;
    }

    // Branch to 'branchDestOrd' if 'testReg' is non-zero
    //
    void InitCondBranch(uint32_t selfOrd, uint32_t defaultDestOrd, uint32_t branchDestOrd)
    {
        TestAssert(m_kind == Kind::X_END_OF_ENUM);
        if (defaultDestOrd == branchDestOrd)
        {
            // This branch is in fact unconditional
            //
            InitUnconditionalBranch(selfOrd, defaultDestOrd);
            return;
        }
        m_kind = Kind::CondBr;
        if (branchDestOrd == selfOrd + 1)
        {
            // Test for zero instead, and flip defaultDestOrd and branchDestOrd, so now the default dest is fallthrough
            //
            m_isTestForNonZero = false;
            m_isDefaultTargetFallthrough = true;
            m_defaultTargetOrd = branchDestOrd;
            m_branchTargetOrd = defaultDestOrd;
        }
        else
        {
            m_isTestForNonZero = true;
            m_isDefaultTargetFallthrough = (defaultDestOrd == selfOrd + 1);
            m_defaultTargetOrd = defaultDestOrd;
            m_branchTargetOrd = branchDestOrd;
        }
    }

    void InitNoSuccessor()
    {
        TestAssert(m_kind == Kind::X_END_OF_ENUM);
        m_kind = Kind::NoSuccessor;
    }

    uint32_t WARN_UNUSED GetJITCodeLength()
    {
        TestAssert(m_kind != Kind::X_END_OF_ENUM);
        if (m_kind == Kind::NoSuccessor)
        {
            // 'ud2' instruction: 2 bytes
            //
            return 2;
        }
        else if (m_kind == Kind::UncondBr)
        {
            if (m_isDefaultTargetFallthrough)
            {
                // A fallthrough, no code needed
                //
                return 0;
            }
            else
            {
                // A jmp instruction, 5 bytes
                //
                return 5;
            }
        }
        else
        {
            TestAssert(m_kind == Kind::CondBr);
            TestAssert(m_isTestValueInRegInitialized);
            // If test value in reg, 'testq %r64, %r64' is 3 bytes.
            // Otherwise, 'cmpq $0, imm32(%r64)' is 8 bytes
            //
            uint32_t instLen = (m_isTestValueInReg ? 3U : 8U);
            // We always need a conditional branch (jne or je) to m_branchTarget, 6 bytes
            //
            instLen += 6;
            // If m_defaultTarget is not fallthrough, another 5 bytes for the jmp instruction
            //
            if (!m_isDefaultTargetFallthrough)
            {
                instLen += 5;
            }
            return instLen;
        }
    }

    void AssertConsistency()
    {
#ifdef TESTBUILD
        TestAssert(m_kind != Kind::X_END_OF_ENUM);
        TestAssertImp(m_kind == Kind::UncondBr, m_defaultTargetOrd != static_cast<uint32_t>(-1));
        if (m_kind == Kind::CondBr)
        {
            TestAssert(m_isTestValueInRegInitialized &&
                       m_defaultTargetOrd != static_cast<uint32_t>(-1) &&
                       m_branchTargetOrd != static_cast<uint32_t>(-1));
            TestAssertImp(m_isTestValueInReg, m_testReg.IsGPR());
            TestAssertImp(!m_isTestValueInReg, m_testPhysicalSlot != static_cast<uint16_t>(-1));
        }
#endif
    }

#ifdef TESTBUILD
    void DumpHumanReadableLog(PrimaryCodegenState& pcs, CodegenLogDumpContext& ctx)
    {
        fprintf(ctx.m_file, "   0x%llx:", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pcs.m_fastPathAddr)));
        TestAssert(m_kind != Kind::X_END_OF_ENUM);
        if (m_kind == Kind::NoSuccessor)
        {
            fprintf(ctx.m_file, "  [Control Flow Ends]\n");
        }
        else if (m_kind == Kind::UncondBr)
        {
            if (m_isDefaultTargetFallthrough)
            {
                fprintf(ctx.m_file, "  [Fallthrough to BasicBlock #%u]\n", static_cast<unsigned int>(m_defaultTargetOrd));
            }
            else
            {
                fprintf(ctx.m_file, "  Branch -> BasicBlock #%u\n", static_cast<unsigned int>(m_defaultTargetOrd));
            }
        }
        else
        {
            TestAssert(m_kind == Kind::CondBr);
            TestAssert(m_isTestValueInRegInitialized);
            if (m_isTestForNonZero)
            {
                fprintf(ctx.m_file, "  BranchIfNotZero ");
            }
            else
            {
                fprintf(ctx.m_file, "  BranchIfZero ");
            }
            if (m_isTestValueInReg)
            {
                fprintf(ctx.m_file, "%s", m_testReg.GetName());
            }
            else
            {
                fprintf(ctx.m_file, "stk[%u]", static_cast<unsigned int>(m_testPhysicalSlot));
            }
            fprintf(ctx.m_file, " -> BasicBlock #%u\n", static_cast<unsigned int>(m_branchTargetOrd));

            if (m_isDefaultTargetFallthrough)
            {
                fprintf(ctx.m_file, "                  [Fallthrough to BasicBlock #%u]\n", static_cast<unsigned int>(m_defaultTargetOrd));
            }
            else
            {
                fprintf(ctx.m_file, "                  Branch -> BasicBlock #%u\n", static_cast<unsigned int>(m_defaultTargetOrd));
            }
        }
    }
#endif

    // Implementation later in this file since it needs access to BasicBlockCodegenInfo
    //
    void EmitJITCode(uint8_t*& addr /*inout*/, uint8_t* jitFastPathBaseAddr, TempVector<BasicBlockCodegenInfo>& bbOrder);

    Kind m_kind;
    // Indicates whether m_isTestValueInReg has been initialized properly (since we initialize this field later than the other fields)
    //
    bool m_isTestValueInRegInitialized;
    // If this is a conditional branch, whether the value to test is in a register
    // If so, it must be a GPR register
    // Only makes sense for CondBr
    //
    bool m_isTestValueInReg;
    // If true, it means the conditional branch should be taken (i.e., branches to branchTarget) if the test value is nonzero
    // If false, the branch should be taken if test value is zero
    // Only makes sense for CondBr
    //
    bool m_isTestForNonZero;
    // Whether the defaultTarget is the fallthrough basic block
    // Only makes sense for CondBr and UncondBr.
    // Note that the branchTarget is never a fallthrough: we can always workaround this by flipping the condition and target.
    //
    bool m_isDefaultTargetFallthrough;
    // If CondBr and test value is in reg, the GPR register that holds the test value
    //
    X64Reg m_testReg;
    // If CondBr and test value is on stack, the physical slot ordinal
    //
    uint16_t m_testPhysicalSlot;
    // The ordinal into the BasicBlockTerminatorCodegenInfo list for the default target, only makes sense for CondBr and UncondBr
    //
    uint32_t m_defaultTargetOrd;
    // The ordinal into the BasicBlockTerminatorCodegenInfo list for the branch target, only makes sense for CondBr
    //
    uint32_t m_branchTargetOrd;
};

struct BasicBlockCodegenInfo
{
    BasicBlockCodegenInfo()
        : m_bb(nullptr)
        , m_codegenLog()
        , m_slowPathDataRegConfStream()
        , m_terminatorInfo()
        , m_fastPathStartOffset(static_cast<uint32_t>(-1))
        , m_slowPathStartOffset(static_cast<uint32_t>(-1))
        , m_dataSecStartOffset(static_cast<uint32_t>(-1))
        , m_slowPathDataStartOffset(static_cast<uint32_t>(-1))
        , m_isOnDfsStack(false)
        , m_isReachedByBackEdge(false)
        , m_shouldPadFastPathTo16ByteAlignmentAtEnd(false)
        , m_addNopBytesAtEnd(static_cast<uint8_t>(-1))
    { }

    BasicBlock* m_bb;

    // The codegen log to generate code for this basic block (excluding terminator)
    //
    std::span<uint8_t> m_codegenLog;

    // The SlowPathDataRegConfig stream to be used during the code generation
    //
    std::span<uint8_t> m_slowPathDataRegConfStream;

    // The terminator logic for this basic block
    //
    BasicBlockTerminatorCodegenInfo m_terminatorInfo;

    // The start offset of each JIT code section
    //
    uint32_t m_fastPathStartOffset;
    uint32_t m_slowPathStartOffset;
    uint32_t m_dataSecStartOffset;

    // For assertion purposes
    //
    uint32_t m_expectedFastPathOffsetAfterLogReplay;
    uint32_t m_expectedSlowPathOffsetAfterLogReplay;
    uint32_t m_expectedDataSecOffsetAfterLogReplay;
    uint32_t m_expectedSlowPathDataOffsetAfterLogReplay;

    // The start offset in the SlowPathData stream
    //
    uint32_t m_slowPathDataStartOffset;

    // Only used to decide m_isReachedByBackEdge
    //
    bool m_isOnDfsStack;

    // True if a back edge points to this basic block
    //
    bool m_isReachedByBackEdge;

    // If true, it means we should add NOPs after the fast path to make the next basic block start at a 16-byte boundary
    //
    bool m_shouldPadFastPathTo16ByteAlignmentAtEnd;

    // If m_shouldPadFastPathTo16ByteAlignmentAtEnd is true, how many bytes of NOP we should add, for assertion purpose.
    // As such, this value is always <=15 and as long as code is generated correctly,
    // the fastPathOffset will always be a multiple of 16 after padding this many bytes of NOPs
    //
    uint8_t m_addNopBytesAtEnd;
};

void BasicBlockTerminatorCodegenInfo::EmitJITCode(uint8_t*& addr /*inout*/, uint8_t* jitFastPathBaseAddr, TempVector<BasicBlockCodegenInfo>& bbOrder)
{
    auto getDestAddr = [&](uint32_t bbOrd) ALWAYS_INLINE WARN_UNUSED -> uint8_t*
    {
        TestAssert(bbOrd < bbOrder.size());
        TestAssert(bbOrder[bbOrd].m_fastPathStartOffset != static_cast<uint32_t>(-1));
        return jitFastPathBaseAddr + bbOrder[bbOrd].m_fastPathStartOffset;
    };

    [[maybe_unused]] uint8_t* oldAddr = addr;
    if (m_kind == Kind::NoSuccessor)
    {
        EmitUd2Instruction(addr /*inout*/);
    }
    else if (m_kind == Kind::UncondBr)
    {
        if (!m_isDefaultTargetFallthrough)
        {
            EmitJmpInstruction(addr /*inout*/, getDestAddr(m_defaultTargetOrd));
        }
    }
    else
    {
        TestAssert(m_kind == Kind::CondBr);
        TestAssert(m_isTestValueInRegInitialized);
        if (m_isTestValueInReg)
        {
            EmitTestRegRegInstruction(addr /*inout*/, m_testReg);
        }
        else
        {
            uint32_t byteOffset = static_cast<uint32_t>(sizeof(TValue) * m_testPhysicalSlot);
            EmitI64CmpBaseImm32OffsetZeroInstruction(addr /*inout*/, x_dfg_stack_base_register, byteOffset);
        }

        uint8_t* branchTargetAddr = getDestAddr(m_branchTargetOrd);
        if (m_isTestForNonZero)
        {
            EmitJneInstruction(addr /*inout*/, branchTargetAddr);
        }
        else
        {
            EmitJeInstruction(addr /*inout*/, branchTargetAddr);
        }

        if (!m_isDefaultTargetFallthrough)
        {
            EmitJmpInstruction(addr /*inout*/, getDestAddr(m_defaultTargetOrd));
        }
    }
    TestAssert(addr == oldAddr + GetJITCodeLength());
}

struct DfgBackend
{
    static DfgBackendResult Run(TempArenaAllocator& resultAlloc, Graph* graph, StackLayoutPlanningResult& stackLayoutPlanningResult)
    {
        DfgBackend impl(resultAlloc, graph, stackLayoutPlanningResult);
        impl.RunImpl();
        DfgBackendResult r;
        r.m_dfgCodeBlock = impl.m_resultDcb;
#ifdef TESTBUILD
        r.m_codegenLogDump = impl.m_codegenLogDumpPtr;
        r.m_codegenLogDumpSize = impl.m_codegenLogDumpSize;
#endif
        return r;
    }

private:
    DfgBackend(TempArenaAllocator& resultAlloc, Graph* graph, StackLayoutPlanningResult& stackLayoutPlanningResult)
        : m_resultAlloc(resultAlloc)
        , m_passAlloc()
        , m_bbAlloc()
        , m_stackLayoutPlanningResult(stackLayoutPlanningResult)
        , m_valueUseListBuilder(m_passAlloc, m_bbAlloc, graph)
        , m_manager(m_passAlloc,
                    SafeIntegerCast<uint16_t>(graph->GetTotalNumInterpreterSlots()) /*numShadowStackSlots*/,
                    SafeIntegerCast<uint16_t>(graph->GetNumFixedArgsInRootFunction()) /*firstRegSpillPhysicalSlot*/,
                    SafeIntegerCast<uint16_t>(graph->GetNumPhysicalSlotsForLocals()) /*numPhysicalSlotsForLocals*/)
        , m_gprAlloc(m_passAlloc, m_manager)
        , m_fprAlloc(m_passAlloc, m_manager)
        , m_nodeOutputtingBranchDecision(nullptr)
        , m_currentUseIndex(0)
        , m_firstRegSpillSlot(SafeIntegerCast<uint16_t>(graph->GetNumFixedArgsInRootFunction()))
        , m_totalJitFastPathSectionLen(static_cast<uint32_t>(-1))
        , m_totalJitSlowPathSectionLen(static_cast<uint32_t>(-1))
        , m_totalJitDataSectionLen(static_cast<uint32_t>(-1))
        , m_slowPathDataStartOffset(0)
        , m_slowPathDataEndOffset(0)
        , m_functionEntryTrait()
        , m_rangeOpInfoDecoder(m_passAlloc)
        , m_isRangeOpSlotHoldingOutput(m_passAlloc)
        , m_createFnObjUvIndexList(m_passAlloc)
        , m_literalFieldToBeAddedByTotalFrameSlots(m_passAlloc)
        , m_bbOrder(m_passAlloc)
        , m_resultDcb(nullptr)
#ifdef TESTBUILD
        , m_codegenLogDumpContext()
        , m_codegenLogDumpPtr(nullptr)
        , m_codegenLogDumpSize(0)
#endif
    {
        m_functionEntryTrait.m_emitterFn = nullptr;
    }

    void InitializeUseListBuilder()
    {
        m_valueUseListBuilder.SetupUseInfoForAllConstants();
    }

    void AdvanceCurrentUseIndex()
    {
        TestAssert(*m_nextSpillEverythingUseIndex >= m_currentUseIndex);
        if (*m_nextSpillEverythingUseIndex == m_currentUseIndex)
        {
            TestAssert(m_nextSpillEverythingUseIndex > m_valueUseListBuilder.m_nextSpillAllUseIndexList.data());
            m_nextSpillEverythingUseIndex--;
            TestAssert(*m_nextSpillEverythingUseIndex > m_currentUseIndex);
        }
        m_currentUseIndex++;
    }

    // Generate code for each type check
    // Note that we must generate the checks in the order they show up
    //
    void EmitAllTypeChecks(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        m_gprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        m_fprAlloc.AssertMinimumUseIndex(m_currentUseIndex);

        uint32_t curInputOrd = 0;
        for (ValueUseRAInfo op : nodeInfo->GetCheckOperands())
        {
            // Type checks are emitted one by one, and we never mark duplicate edge even if two checks operate on the same SSA value
            //
            TestAssert(!op.IsDuplicateEdge());

            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            m_manager.AssertValueLiveAndRegInfoOK(ssaVal, true /*constantMaybeUnmaterialized*/);
            TestAssertImp(op.IsGPRUse(), ssaVal->GetGprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
            TestAssertImp(!op.IsGPRUse(), ssaVal->GetFprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);

            // Figure out which edge this check is for
            //
            while (true)
            {
                TestAssert(curInputOrd < node->GetNumInputs());
                if (node->GetInputEdge(curInputOrd).NeedsTypeCheck())
                {
                    break;
                }
                curInputOrd++;
            }
            Edge& edge = node->GetInputEdge(curInputOrd);
            curInputOrd++;

            TestAssert(edge.NeedsTypeCheck());
            const DfgVariantTraits* trait = GetTypeCheckCodegenInfoForUseKind(edge.GetUseKind());

            TestAssert(ShouldTypeCheckOperandUseGPR(edge.GetUseKind()) == op.IsGPRUse());

            TestAssert(trait->IsRegAllocEnabled());
            TestAssert(trait->NumOperandsForRA() == 1);
            TestAssert(!trait->HasOutput());
            TestAssert(!trait->Operand(0).HasChoices());
            TestAssertIff(op.IsGPRUse(), trait->Operand(0).GprAllowed());
            TestAssert(trait->GetTotalNumRegBankSubVariants() == 1);

            const DfgRegBankSubVariantTraits* svTrait = trait->GetRegBankSubVariant(0);
            uint8_t numGprScratchNeeded = svTrait->NumGprScratchNeeded();
            uint8_t numFprScratchNeeded = svTrait->NumFprScratchNeeded();
            bool shouldRelocateAllGroup1Gprs = svTrait->ShouldRelocateAllGroup1Gprs();

            if (op.IsGPRUse())
            {
                RegAllocTypecheckCodegenDecisions gprInfo = m_gprAlloc.WorkForCodegenCheck(op, numGprScratchNeeded, shouldRelocateAllGroup1Gprs);
                RegAllocPassthruAndScratchRegInfo fprInfo = m_fprAlloc.EvictUntil(numFprScratchNeeded, false /*shouldAlsoRelocateAllGroup1Gprs*/);
                TestAssert(gprInfo.m_inputRegIdx < x_dfg_reg_alloc_num_gprs);
                X64Reg opReg = x_dfg_reg_alloc_gprs[gprInfo.m_inputRegIdx];
                TestAssert(ssaVal->GetGprRegister() == opReg);

                size_t numGroup1GprPassthrus = CountNumberOfOnes(gprInfo.m_nonOperandRegInfo.m_group1PassthruIdxMask);
                TestAssertImp(shouldRelocateAllGroup1Gprs, numGroup1GprPassthrus == 0 && opReg.MachineOrd() >= 8);

                // The table is a 2 [InputGroup1/2] * (x_dfg_reg_alloc_num_group1_gprs + 1) [#Group1Passthrus] array
                //
                TestAssert(svTrait->GetTableLength() == 2 * (x_dfg_reg_alloc_num_group1_gprs + 1));
                size_t idx1 = (opReg.MachineOrd() < 8) ? 0 : 1;
                size_t idx2 = numGroup1GprPassthrus;
                size_t idx = idx1 * (x_dfg_reg_alloc_num_group1_gprs + 1) + idx2;
                DfgCodegenFuncOrd cgFnOrd = svTrait->GetTrailingArrayElement(idx);

                CodegenLog().EmitTypeCheck(cgFnOrd, opReg, gprInfo.m_nonOperandRegInfo, fprInfo);
            }
            else
            {
                RegAllocTypecheckCodegenDecisions fprInfo = m_fprAlloc.WorkForCodegenCheck(op, numFprScratchNeeded, false /*shouldAlsoRelocateAllGroup1Gprs*/);
                RegAllocPassthruAndScratchRegInfo gprInfo = m_gprAlloc.EvictUntil(numGprScratchNeeded, shouldRelocateAllGroup1Gprs);

                TestAssert(fprInfo.m_inputRegIdx < x_dfg_reg_alloc_num_fprs);
                X64Reg opReg = x_dfg_reg_alloc_fprs[fprInfo.m_inputRegIdx];
                TestAssert(ssaVal->GetFprRegister() == opReg);

                size_t numGroup1GprPassthrus = CountNumberOfOnes(gprInfo.m_group1PassthruIdxMask);
                TestAssertImp(shouldRelocateAllGroup1Gprs, numGroup1GprPassthrus == 0);

                // The table is simply an (x_dfg_reg_alloc_num_group1_gprs + 1) [#Group1Passthrus] array
                //
                TestAssert(svTrait->GetTableLength() == (x_dfg_reg_alloc_num_group1_gprs + 1));
                size_t idx = numGroup1GprPassthrus;
                DfgCodegenFuncOrd cgFnOrd = svTrait->GetTrailingArrayElement(idx);

                CodegenLog().EmitTypeCheck(cgFnOrd, opReg, gprInfo, fprInfo.m_nonOperandRegInfo);
            }
        }
#ifdef TESTBUILD
        // Assert that no more edges should need type check
        //
        while (curInputOrd < node->GetNumInputs())
        {
            TestAssert(!node->GetInputEdge(curInputOrd).NeedsTypeCheck());
            curInputOrd++;
        }
        TestAssert(curInputOrd == node->GetNumInputs());

        // Assert that NextUseIndex should be strictly larger than useIndex now
        //
        for (ValueUseRAInfo op : nodeInfo->GetCheckOperands())
        {
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            TestAssertImp(op.IsGPRUse(), ssaVal->GetGprNextUseInfo().GetNextUseIndex() > m_currentUseIndex);
            TestAssertImp(!op.IsGPRUse(), ssaVal->GetFprNextUseInfo().GetNextUseIndex() > m_currentUseIndex);
        }
#endif
        // Advance to next useIndex
        //
        AdvanceCurrentUseIndex();
    }

    struct RegConfigResult
    {
        DfgCodegenFuncOrd m_cgFnOrd;
        RegAllocMainCodegenDecisions m_gprInfo;
        RegAllocMainCodegenDecisions m_fprInfo;
        const DfgRegBankSubVariantTraits* m_svTrait;
        ValueRegAllocInfo* m_outputVal;
        ValueRegAllocInfo* m_brDecisionOutputVal;

        // Setup the RegAllocRegConfig and NodeOperandConfigData field in the codegen operation descriptor
        //
        void SetupCodegenConfigInfo(RegAllocRegConfig& regInfo /*out*/,
                                    NodeOperandConfigData& operandInfo /*out*/,
                                    uint16_t firstRegSpillSlot,
                                    uint16_t rangeOperandPhysicalSlot)
        {
            CodegenOperationLog::InitRegPassthruAndScratchInfo(regInfo /*out*/,
                                                               m_gprInfo.m_nonOperandRegInfo,
                                                               m_fprInfo.m_nonOperandRegInfo);

            const DfgRegBankSubVariantTraits* svTrait = m_svTrait;
            size_t numOperands = svTrait->NumRAOperands();

            operandInfo.m_numInputOperands = static_cast<uint8_t>(numOperands);
            operandInfo.m_codegenFuncOrd = m_cgFnOrd;
            operandInfo.m_rangeOperandPhysicalSlot = rangeOperandPhysicalSlot;

            size_t curGprOpOrd = 0, curFprOpOrd = 0;
            for (size_t opIdx = 0; opIdx < numOperands; opIdx++)
            {
                if (svTrait->OperandIsGPR(opIdx))
                {
                    uint8_t regIdx = m_gprInfo.GetInputRegIdx(curGprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
                    curGprOpOrd++;
                    regInfo.SetOperandReg(opIdx, x_dfg_reg_alloc_gprs[regIdx]);
                    operandInfo.m_inputOperandPhysicalSlots[opIdx] = firstRegSpillSlot + regIdx;
                }
                else
                {
                    uint8_t regIdx = m_fprInfo.GetInputRegIdx(curFprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
                    curFprOpOrd++;
                    regInfo.SetOperandReg(opIdx, x_dfg_reg_alloc_fprs[regIdx]);
                    operandInfo.m_inputOperandPhysicalSlots[opIdx] = firstRegSpillSlot + x_dfg_reg_alloc_num_gprs + regIdx;
                }
            }
            TestAssert(curGprOpOrd == m_gprInfo.m_numInputRegsInThisBank && curFprOpOrd == m_fprInfo.m_numInputRegsInThisBank);

            size_t curRaOpIdx = numOperands;
            if (svTrait->HasOutput())
            {
                if (svTrait->IsOutputGPR())
                {
                    TestAssert(m_gprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_gprs);
                    operandInfo.m_outputPhysicalSlot = firstRegSpillSlot + m_gprInfo.m_outputRegIdx;
                    // If the output reused an input register, it doesn't have its own register in RegConfig,
                    // but raOpIdx still increments (and same for the FPR branch as well)
                    //
                    if (!m_gprInfo.m_outputReusedInputRegister)
                    {
                        regInfo.SetOperandReg(curRaOpIdx, x_dfg_reg_alloc_gprs[m_gprInfo.m_outputRegIdx]);
                    }
                }
                else
                {
                    TestAssert(m_fprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_fprs);
                    operandInfo.m_outputPhysicalSlot = firstRegSpillSlot + x_dfg_reg_alloc_num_gprs + m_fprInfo.m_outputRegIdx;
                    if (!m_fprInfo.m_outputReusedInputRegister)
                    {
                        regInfo.SetOperandReg(curRaOpIdx, x_dfg_reg_alloc_fprs[m_fprInfo.m_outputRegIdx]);
                    }
                }
                curRaOpIdx++;
            }

            if (svTrait->HasBrDecision())
            {
                TestAssert(m_gprInfo.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs);
                operandInfo.m_brDecisionPhysicalSlot = firstRegSpillSlot + m_gprInfo.m_brDecisionRegIdx;
                if (!m_gprInfo.m_brDecisionReusedInputRegister)
                {
                    regInfo.SetOperandReg(curRaOpIdx, x_dfg_reg_alloc_gprs[m_gprInfo.m_brDecisionRegIdx]);
                }
                curRaOpIdx++;
            }

            regInfo.AssertConsistency();

#ifdef TESTBUILD
            // As an extra sanity check, validate that the 'regInfo' can be populated into a valid codegen state
            // and that the state is in consistency with what is reported by GetRegPurposeInfo()
            //
            {
                RegAllocStateForCodeGen cgState;
                regInfo.PopulateCodegenState(cgState /*out*/);
                RegAllocAllRegPurposeInfo regPurposeInfo = GetRegPurposeInfo();
                using RegClass = RegAllocAllRegPurposeInfo::RegClass;
                for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; i++)
                {
                    RegClass regClass = regPurposeInfo.m_data[i].m_regClass;
                    uint8_t ordInClass = regPurposeInfo.m_data[i].m_ordInClass;
                    TestAssert(regClass < RegClass::X_END_OF_ENUM);
                    TestAssert(ordInClass < 8);
                    TestAssert(cgState.Get(regClass, ordInClass) == (GetDfgRegFromRegAllocSequenceOrd(i).MachineOrd() & 7));
                    cgState.Unset(regClass, ordInClass);
                }
                for (size_t i = 0; i < cgState.x_stateLength; i++)
                {
                    TestAssert(cgState.m_data[i] == cgState.x_invalidValue);
                }
            }
#endif
        }

        // Unfortunately this largely replicates the logic in SetupCodegenConfigInfo
        // For now, we sanity check the logic is in sync by letting SetupCodegenConfigInfo check consistency when it is called
        //
        RegAllocAllRegPurposeInfo WARN_UNUSED GetRegPurposeInfo()
        {
            using RegClass = RegAllocAllRegPurposeInfo::RegClass;
            RegAllocAllRegPurposeInfo r;
            static_assert(RegAllocAllRegPurposeInfo::x_length >= x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
#ifdef TESTBUILD
            for (size_t i = 0; i < r.x_length; i++)
            {
                r.m_data[i] = { .m_regClass = RegClass::X_END_OF_ENUM, .m_ordInClass = 0 };
            }
#endif
            const DfgRegBankSubVariantTraits* svTrait = m_svTrait;
            size_t numOperands = svTrait->NumRAOperands();
            size_t curGprOpOrd = 0, curFprOpOrd = 0;
            for (size_t opIdx = 0; opIdx < numOperands; opIdx++)
            {
                if (svTrait->OperandIsGPR(opIdx))
                {
                    uint8_t regIdx = m_gprInfo.GetInputRegIdx(curGprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
                    curGprOpOrd++;
                    TestAssert(r.m_data[regIdx].m_regClass == RegClass::X_END_OF_ENUM);
                    r.m_data[regIdx] = { .m_regClass = RegClass::Operand, .m_ordInClass = static_cast<uint8_t>(opIdx) };
                }
                else
                {
                    uint8_t regIdx = m_fprInfo.GetInputRegIdx(curFprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
                    curFprOpOrd++;
                    TestAssert(r.m_data[x_dfg_reg_alloc_num_gprs + regIdx].m_regClass == RegClass::X_END_OF_ENUM);
                    r.m_data[x_dfg_reg_alloc_num_gprs + regIdx] = { .m_regClass = RegClass::Operand, .m_ordInClass = static_cast<uint8_t>(opIdx) };
                }
            }
            TestAssert(curGprOpOrd == m_gprInfo.m_numInputRegsInThisBank && curFprOpOrd == m_fprInfo.m_numInputRegsInThisBank);

            size_t curRaOpIdx = numOperands;
            if (svTrait->HasOutput())
            {
                if (svTrait->IsOutputGPR())
                {
                    TestAssert(m_gprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_gprs);
                    if (!m_gprInfo.m_outputReusedInputRegister)
                    {
                        TestAssert(r.m_data[m_gprInfo.m_outputRegIdx].m_regClass == RegClass::X_END_OF_ENUM);
                        r.m_data[m_gprInfo.m_outputRegIdx] = {
                            .m_regClass = RegClass::Operand,
                            .m_ordInClass = static_cast<uint8_t>(curRaOpIdx)
                        };
                    }
                }
                else
                {
                    TestAssert(m_fprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_fprs);
                    if (!m_fprInfo.m_outputReusedInputRegister)
                    {
                        TestAssert(r.m_data[x_dfg_reg_alloc_num_gprs + m_fprInfo.m_outputRegIdx].m_regClass == RegClass::X_END_OF_ENUM);
                        r.m_data[x_dfg_reg_alloc_num_gprs + m_fprInfo.m_outputRegIdx] = {
                            .m_regClass = RegClass::Operand,
                            .m_ordInClass = static_cast<uint8_t>(curRaOpIdx)
                        };
                    }
                }
                curRaOpIdx++;
            }

            if (svTrait->HasBrDecision())
            {
                TestAssert(m_gprInfo.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs);
                if (!m_gprInfo.m_brDecisionReusedInputRegister)
                {
                    TestAssert(r.m_data[m_gprInfo.m_brDecisionRegIdx].m_regClass == RegClass::X_END_OF_ENUM);
                    r.m_data[m_gprInfo.m_brDecisionRegIdx] = {
                        .m_regClass = RegClass::Operand,
                        .m_ordInClass = static_cast<uint8_t>(curRaOpIdx)
                    };
                }
                curRaOpIdx++;
            }

            auto processNonOperandRegs = [&](RegClass regClass, size_t seqOrdBase, uint8_t mask) ALWAYS_INLINE
            {
                uint8_t opIdx = 0;
                while (mask != 0)
                {
                    size_t regIdxInClass = CountTrailingZeros(mask);
                    mask ^= static_cast<uint8_t>(1U << regIdxInClass);
                    size_t seqOrd = seqOrdBase + regIdxInClass;
                    TestAssert(seqOrd < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
                    TestAssert(r.m_data[seqOrd].m_regClass == RegClass::X_END_OF_ENUM);
                    r.m_data[seqOrd] = { .m_regClass = regClass, .m_ordInClass = opIdx };
                    opIdx++;
                }
            };

            processNonOperandRegs(RegClass::ScExtG, 0 /*seqOrdBase*/, m_gprInfo.m_nonOperandRegInfo.m_group2ScratchIdxMask);
            processNonOperandRegs(RegClass::PtExtG, 0 /*seqOrdBase*/, m_gprInfo.m_nonOperandRegInfo.m_group2PassthruIdxMask);
            size_t group1GprStartSeqOrd = x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs;
            processNonOperandRegs(RegClass::ScNonExtG, group1GprStartSeqOrd, m_gprInfo.m_nonOperandRegInfo.m_group1ScratchIdxMask);
            processNonOperandRegs(RegClass::PtNonExtG, group1GprStartSeqOrd, m_gprInfo.m_nonOperandRegInfo.m_group1PassthruIdxMask);
            processNonOperandRegs(RegClass::ScF, x_dfg_reg_alloc_num_gprs /*seqOrdBase*/, m_fprInfo.m_nonOperandRegInfo.m_group1ScratchIdxMask);
            processNonOperandRegs(RegClass::PtF, x_dfg_reg_alloc_num_gprs /*seqOrdBase*/, m_fprInfo.m_nonOperandRegInfo.m_group1PassthruIdxMask);

#ifdef TESTBUILD
            for (size_t i = 0; i < r.x_length; i++)
            {
                TestAssertIff(i < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs, r.m_data[i].m_regClass != RegClass::X_END_OF_ENUM);
            }
#endif
            // Caller logic expects us to fill the dummy entries at the end with <RegClass::X_END_OF_ENUM, 0>
            //
            for (size_t i = x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; i < r.x_length; i++)
            {
                r.m_data[i] = { .m_regClass = RegClass::X_END_OF_ENUM, .m_ordInClass = 0 };
            }
            return r;
        }

        void UpdateStateAfterCodegen(RegAllocValueManager& manager,
                                     RegAllocDecisionMaker<true /*forGprState*/, RegAllocValueManager>& gprAlloc,
                                     RegAllocDecisionMaker<false /*forGprState*/, RegAllocValueManager>& fprAlloc)
        {
            const DfgRegBankSubVariantTraits* svTrait = m_svTrait;

            // If the node outputs an output or brDecision, it is born now
            //
            if (svTrait->HasOutput())
            {
                TestAssert(m_outputVal != nullptr);
                if (svTrait->IsOutputGPR())
                {
                    manager.ProcessBornInRegister<true /*forGprState*/>(m_outputVal, m_gprInfo.m_outputRegIdx);
                }
                else
                {
                    manager.ProcessBornInRegister<false /*forFprState*/>(m_outputVal, m_fprInfo.m_outputRegIdx);
                }
            }
            if (svTrait->HasBrDecision())
            {
                TestAssert(m_brDecisionOutputVal != nullptr);
                manager.ProcessBornInRegister<true /*forGprState*/>(m_brDecisionOutputVal, m_gprInfo.m_brDecisionRegIdx);
            }
            manager.UpdateRegisterStateAfterNodeExecutedAndOutputsBorn();
            gprAlloc.ClearOutputRegMaskAfterBornEvents();
            fprAlloc.ClearOutputRegMaskAfterBornEvents();
        }
    };

    // Do register allocation to fulfill codegen requirements
    // If the node does not have output/brDecisionOutput, nullptr should be provided
    //
    RegConfigResult WARN_UNUSED ConfigureRegistersForCodegen(const DfgVariantTraits* trait,
                                                             std::span<ValueUseRAInfo> operands,
                                                             ValueRegAllocInfo* outputVal,
                                                             bool outputShouldUseGpr,
                                                             ValueRegAllocInfo* brDecisionOutputVal)
    {
        TestAssert(trait != nullptr);
        TestAssert(trait->IsRegAllocEnabled());

        TestAssert(trait->NumOperandsForRA() == operands.size());

        RegAllocMainCodegenDesc gprDesc;
        RegAllocMainCodegenDesc fprDesc;

        size_t numOperands = operands.size();

        size_t regBankSubvariantIdx = 0;
        [[maybe_unused]] size_t expectedRegBankSubvariantTableSize = 1;
        for (uint8_t opIdx = 0; opIdx < numOperands; opIdx++)
        {
            ValueUseRAInfo op = operands[opIdx];

            if (trait->Operand(opIdx).HasChoices())
            {
                expectedRegBankSubvariantTableSize *= 2;
                regBankSubvariantIdx *= 2;
                if (!op.IsGPRUse())
                {
                    regBankSubvariantIdx++;
                }
            }

            if (op.IsGPRUse())
            {
                TestAssert(trait->Operand(opIdx).GprAllowed());
                gprDesc.m_operands[gprDesc.m_numOperands] = op;
                gprDesc.m_numOperands++;
            }
            else
            {
                TestAssert(trait->Operand(opIdx).FprAllowed());
                fprDesc.m_operands[fprDesc.m_numOperands] = op;
                fprDesc.m_numOperands++;
            }
        }
        TestAssertIff(outputVal != nullptr, trait->HasOutput());
        if (trait->HasOutput())
        {
            TestAssertImp(outputShouldUseGpr, trait->Output().GprAllowed());
            TestAssertImp(!outputShouldUseGpr, trait->Output().FprAllowed());
            if (trait->Output().HasChoices())
            {
                expectedRegBankSubvariantTableSize *= 2;
                regBankSubvariantIdx *= 2;
                if (!outputShouldUseGpr)
                {
                    regBankSubvariantIdx++;
                }
            }
        }

        // Select the reg bank subvariant
        //
        TestAssert(trait->GetTotalNumRegBankSubVariants() == expectedRegBankSubvariantTableSize);
        const DfgRegBankSubVariantTraits* svTrait = trait->GetRegBankSubVariant(regBankSubvariantIdx);

#ifdef TESTBUILD
        TestAssert(svTrait->NumRAOperands() == numOperands);
        for (uint8_t opIdx = 0; opIdx < numOperands; opIdx++)
        {
            ValueUseRAInfo op = operands[opIdx];
            TestAssertIff(op.IsGPRUse(), svTrait->OperandIsGPR(opIdx));
        }
        TestAssertIff(trait->HasOutput(), svTrait->HasOutput());
        if (trait->HasOutput())
        {
            TestAssertIff(outputShouldUseGpr, svTrait->IsOutputGPR());
        }
        TestAssertIff(brDecisionOutputVal != nullptr, svTrait->HasBrDecision());
#endif

        if (trait->HasOutput())
        {
            if (outputShouldUseGpr)
            {
                TestAssert(trait->Output().GprAllowed());
                gprDesc.m_hasOutput = true;
                gprDesc.m_outputMayReuseInputReg = svTrait->OutputMayReuseInputReg();
                gprDesc.m_outputPrefersReuseInputReg = svTrait->OutputPrefersReuseInputReg();
                gprDesc.m_outputVal = outputVal;
            }
            else
            {
                TestAssert(trait->Output().FprAllowed());
                fprDesc.m_hasOutput = true;
                fprDesc.m_outputMayReuseInputReg = svTrait->OutputMayReuseInputReg();
                fprDesc.m_outputPrefersReuseInputReg = svTrait->OutputPrefersReuseInputReg();
                fprDesc.m_outputVal = outputVal;
            }
        }

        if (svTrait->HasBrDecision())
        {
            gprDesc.m_hasBrDecisionOutput = true;
            gprDesc.m_brDecisionMayReuseInputReg = svTrait->BrDecisionMayReuseInputReg();
            gprDesc.m_brDecisionPrefersReuseInputReg = svTrait->BrDecisionPrefersReuseInputReg();
            TestAssert(m_valueUseListBuilder.m_brDecision != nullptr);
            gprDesc.m_brDecisionVal = m_valueUseListBuilder.m_brDecision;
        }

        gprDesc.m_shouldTurnAllGprGroup1RegsToScratch = svTrait->ShouldRelocateAllGroup1Gprs();

        gprDesc.m_numScratchRegistersRequired = svTrait->NumGprScratchNeeded();
        fprDesc.m_numScratchRegistersRequired = svTrait->NumFprScratchNeeded();

        TestAssert(*m_nextSpillEverythingUseIndex >= m_currentUseIndex);
        gprDesc.m_nextSpillEverythingIndex = *m_nextSpillEverythingUseIndex;
        fprDesc.m_nextSpillEverythingIndex = *m_nextSpillEverythingUseIndex;

        gprDesc.m_useIndex = m_currentUseIndex;
        fprDesc.m_useIndex = m_currentUseIndex;

        RegConfigResult r;
        RegAllocMainCodegenDecisions& gprInfo = r.m_gprInfo;
        RegAllocMainCodegenDecisions& fprInfo = r.m_fprInfo;

        // Perform register operations to fulfill our requirement
        //
        gprInfo = m_gprAlloc.WorkForCodegen(gprDesc);
        fprInfo = m_fprAlloc.WorkForCodegen(fprDesc);

#ifdef TESTBUILD
        // Validate that the operands are indeed available in the registers as claimed by gprInfo and fprInfo
        //
        {
            size_t curGprOpOrd = 0;
            size_t curFprOpOrd = 0;
            for (size_t i = 0; i < numOperands; i++)
            {
                ValueUseRAInfo op = operands[i];
                if (op.IsGPRUse())
                {
                    TestAssert(curGprOpOrd < gprDesc.m_numOperands);
                    uint8_t regIdx = gprInfo.GetInputRegIdx(curGprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
                    TestAssertImp(svTrait->ShouldRelocateAllGroup1Gprs(), regIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                    if (gprInfo.IsInputOrdReusedForOutputOrBrDecision(curGprOpOrd))
                    {
                        m_manager.AssertRegisterEphemerallyHoldingValue<true /*forGprState*/>(op.GetSSAValue(), regIdx);
                    }
                    else
                    {
                        m_manager.AssertRegisterHoldingValue<true /*forGprState*/>(op.GetSSAValue(), regIdx);
                    }
                    curGprOpOrd++;
                }
                else
                {
                    TestAssert(curFprOpOrd < fprDesc.m_numOperands);
                    uint8_t regIdx = fprInfo.GetInputRegIdx(curFprOpOrd);
                    TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
                    if (fprInfo.IsInputOrdReusedForOutputOrBrDecision(curFprOpOrd))
                    {
                        m_manager.AssertRegisterEphemerallyHoldingValue<false /*forGprState*/>(op.GetSSAValue(), regIdx);
                    }
                    else
                    {
                        m_manager.AssertRegisterHoldingValue<false /*forGprState*/>(op.GetSSAValue(), regIdx);
                    }
                    curFprOpOrd++;
                }
            }
            TestAssert(curGprOpOrd == gprDesc.m_numOperands);
            TestAssert(curFprOpOrd == fprDesc.m_numOperands);
        }
        TestAssertImp(svTrait->ShouldRelocateAllGroup1Gprs(), gprInfo.m_nonOperandRegInfo.m_group1PassthruIdxMask == 0);
        TestAssertImp(svTrait->ShouldRelocateAllGroup1Gprs() && gprDesc.m_hasOutput,
                      gprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
        TestAssertImp(svTrait->ShouldRelocateAllGroup1Gprs() && gprDesc.m_hasBrDecisionOutput,
                      gprInfo.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
#endif

        // Compute the concrete codegen function ordinal we need
        // see comments on DfgRegBankSubVariantTraits::GetTrailingArrayElement for more detail
        //
        [[maybe_unused]] size_t expectedCgFnOrdTableLen = 1;
        size_t cgFnOrdTableIdx = 0;
        // Each GPR operand adds a dimension of 2 (Group1 = 0, Group2 = 1)
        //
        for (size_t idx = 0; idx < gprDesc.m_numOperands; idx++)
        {
            TestAssert(gprInfo.GetInputRegIdx(idx) < x_dfg_reg_alloc_num_gprs);
            bool isGroup2 = (gprInfo.GetInputRegIdx(idx) < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
            expectedCgFnOrdTableLen *= 2;
            cgFnOrdTableIdx *= 2;
            if (isGroup2)
            {
                cgFnOrdTableIdx++;
            }
        }
        // If the node has output, add a dimension based on how many choices the output has
        //
        if (svTrait->HasOutput())
        {
            expectedCgFnOrdTableLen *= svTrait->NumOutputChoices();
            cgFnOrdTableIdx *= svTrait->NumOutputChoices();

            if (svTrait->IsOutputGPR())
            {
                TestAssert(gprDesc.m_hasOutput);
                TestAssertImp(svTrait->OutputMayReuseInputReg(), svTrait->NumOutputChoices() == 2 + gprDesc.m_numOperands);
                TestAssertImp(!svTrait->OutputMayReuseInputReg(), svTrait->NumOutputChoices() == 2);
                if (gprInfo.m_outputReusedInputRegister)
                {
                    TestAssert(svTrait->OutputMayReuseInputReg());
                    TestAssert(gprInfo.m_outputReusedInputOperandOrd < gprDesc.m_numOperands);
                    size_t choice = 2 + gprInfo.m_outputReusedInputOperandOrd;
                    TestAssert(choice < svTrait->NumOutputChoices());
                    cgFnOrdTableIdx += choice;
                }
                else
                {
                    TestAssert(gprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_gprs);
                    bool isGroup2Reg = (gprInfo.m_outputRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                    size_t choice = (isGroup2Reg ? 1 : 0);
                    TestAssert(choice < svTrait->NumOutputChoices());
                    cgFnOrdTableIdx += choice;
                }
            }
            else
            {
                TestAssert(fprDesc.m_hasOutput);
                TestAssertImp(svTrait->OutputMayReuseInputReg(), svTrait->NumOutputChoices() == 1 + fprDesc.m_numOperands);
                TestAssertImp(!svTrait->OutputMayReuseInputReg(), svTrait->NumOutputChoices() == 1);
                if (fprInfo.m_outputReusedInputRegister)
                {
                    TestAssert(svTrait->OutputMayReuseInputReg());
                    TestAssert(fprInfo.m_outputReusedInputOperandOrd < fprDesc.m_numOperands);
                    size_t choice = 1 + fprInfo.m_outputReusedInputOperandOrd;
                    TestAssert(choice < svTrait->NumOutputChoices());
                    cgFnOrdTableIdx += choice;
                }
                else
                {
                    size_t choice = 0;
                    TestAssert(choice < svTrait->NumOutputChoices());
                    cgFnOrdTableIdx += choice;
                }
            }
        }
        // If the node outputs a branchDecision, add a dimension based on how many choices it has
        //
        if (svTrait->HasBrDecision())
        {
            expectedCgFnOrdTableLen *= svTrait->NumBrDecisionChoices();
            cgFnOrdTableIdx *= svTrait->NumBrDecisionChoices();

            TestAssertImp(svTrait->BrDecisionMayReuseInputReg(), svTrait->NumBrDecisionChoices() == 2 + gprDesc.m_numOperands);
            TestAssertImp(!svTrait->BrDecisionMayReuseInputReg(), svTrait->NumBrDecisionChoices() == 2);

            TestAssert(gprDesc.m_hasBrDecisionOutput);
            if (gprInfo.m_brDecisionReusedInputRegister)
            {
                TestAssert(svTrait->BrDecisionMayReuseInputReg());
                TestAssert(gprInfo.m_brDecisionReusedInputOperandOrd < gprDesc.m_numOperands);
                size_t choice = 2 + gprInfo.m_brDecisionReusedInputOperandOrd;
                TestAssert(choice < svTrait->NumBrDecisionChoices());
                cgFnOrdTableIdx += choice;
            }
            else
            {
                TestAssert(gprInfo.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs);
                bool isGroup2Reg = (gprInfo.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                size_t choice = (isGroup2Reg ? 1 : 0);
                TestAssert(choice < svTrait->NumBrDecisionChoices());
                cgFnOrdTableIdx += choice;
            }
        }
        // Finally a dimension of size (x_dfg_reg_alloc_num_group1_gprs + 1), the number of group1GprPassthrus
        //
        expectedCgFnOrdTableLen *= (x_dfg_reg_alloc_num_group1_gprs + 1);
        cgFnOrdTableIdx *= (x_dfg_reg_alloc_num_group1_gprs + 1);
        cgFnOrdTableIdx += CountNumberOfOnes(gprInfo.m_nonOperandRegInfo.m_group1PassthruIdxMask);

        TestAssert(svTrait->GetTableLength() == expectedCgFnOrdTableLen);
        r.m_cgFnOrd = svTrait->GetTrailingArrayElement(cgFnOrdTableIdx);
        r.m_svTrait = svTrait;
        r.m_outputVal = outputVal;
        r.m_brDecisionOutputVal = brDecisionOutputVal;
        return r;
    }

    // Emit the main node logic for node that supports reg alloc
    // This assumes that the range operand has been property set up (as given in rangeOperandPhysicalSlot, -1 if doesn't exist)
    // It does not deal with outputs in the range either: caller is responsible for dealing with that
    //
    void EmitMainLogicWithRegAlloc(Node* node,
                                   NodeRegAllocInfo* nodeInfo,
                                   uint16_t rangeOperandPhysicalSlot,
                                   const DfgVariantTraits* trait,
                                   bool shouldEmitRegConfigInSlowPathData)
    {
        m_gprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        m_fprAlloc.AssertMinimumUseIndex(m_currentUseIndex);

        TestAssert(trait != nullptr);
        TestAssert(trait->IsRegAllocEnabled());

        std::span<ValueUseRAInfo> operands = nodeInfo->GetFixedOperands();
        size_t numOperands = operands.size();

        TestAssertIff(trait->HasOutput(), node->HasDirectOutput());
        TestAssert(trait->NumOperandsForRA() == numOperands);

        RegConfigResult rcInfo;
        {
            bool isOutputGpr = false;
            ValueRegAllocInfo* outputVal = nullptr;
            ValueRegAllocInfo* brDecisionVal = nullptr;
            if (node->HasDirectOutput())
            {
                outputVal = node->GetValueRegAllocInfo();
                isOutputGpr = node->ShouldOutputRegisterBankUseGPR();
            }
            if (node == m_nodeOutputtingBranchDecision)
            {
                brDecisionVal = m_valueUseListBuilder.m_brDecision;
            }
            rcInfo = ConfigureRegistersForCodegen(trait, operands, outputVal, isOutputGpr, brDecisionVal);
        }

        if (shouldEmitRegConfigInSlowPathData)
        {
            uint8_t* entry = CodegenLog().AllocateNewSlowPathDataRegConfigEntry();
            EncodeDfgRegAllocStateToSlowPathData(rcInfo.GetRegPurposeInfo(), entry /*out*/);
        }

        CodegenLog().EmitCodegenOpWithRegAlloc(
            static_cast<uint8_t>(numOperands),
            [&](CodegenOpRegAllocEnabled* info) ALWAYS_INLINE
            {
                info->m_nsd = node->GetNodeSpecificDataOrNullptr();
                rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                              info->m_operandConfig /*out*/,
                                              m_firstRegSpillSlot,
                                              rangeOperandPhysicalSlot);
            });

        rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);
    }

    struct SetupResultForNodeWithoutRegAlloc
    {
        DfgCodegenFuncOrd m_cgFnOrd;
        uint8_t m_numOperands;
        uint8_t m_numTemporarySlotsToFree;
        uint16_t m_outputPhysicalSlot;
        uint16_t m_brDecisionPhysicalSlot;
        ValueRegAllocInfo* m_outputVal;
        ValueRegAllocInfo* m_brDecisionOutputVal;
        uint16_t m_physicalSlotForOperands[32];
        uint16_t m_temporarySlotsToFreeLater[32];

        void SetupCodegenInfo(NodeOperandConfigData& nodeConfig /*out*/, uint16_t rangeOperandPhysicalSlot)
        {
            TestAssertIff(m_outputVal != nullptr, m_outputPhysicalSlot != static_cast<uint16_t>(-1));
            TestAssertIff(m_brDecisionOutputVal != nullptr, m_brDecisionPhysicalSlot != static_cast<uint16_t>(-1));
            nodeConfig.m_codegenFuncOrd = m_cgFnOrd;
            nodeConfig.m_numInputOperands = m_numOperands;
            nodeConfig.m_rangeOperandPhysicalSlot = rangeOperandPhysicalSlot;
            nodeConfig.m_outputPhysicalSlot = m_outputPhysicalSlot;
            nodeConfig.m_brDecisionPhysicalSlot = m_brDecisionPhysicalSlot;
            for (size_t i = 0; i < m_numOperands; i++)
            {
                nodeConfig.m_inputOperandPhysicalSlots[i] = m_physicalSlotForOperands[i];
            }
        }

        void UpdateStateAfterCodegen(RegAllocValueManager& manager)
        {
            // Trigger output/brDecisionOutput born event
            //
            TestAssertIff(m_outputVal != nullptr, m_outputPhysicalSlot != static_cast<uint16_t>(-1));
            if (m_outputVal != nullptr)
            {
                manager.ProcessBornOnStack(m_outputVal, m_outputPhysicalSlot);
            }
            TestAssertIff(m_brDecisionOutputVal != nullptr, m_brDecisionPhysicalSlot != static_cast<uint16_t>(-1));
            if (m_brDecisionOutputVal != nullptr)
            {
                manager.ProcessBornOnStack(m_brDecisionOutputVal, m_brDecisionPhysicalSlot);
            }
            // Free all temporary slots used to store constant operands
            //
            for (size_t i = 0; i < m_numTemporarySlotsToFree; i++)
            {
                manager.MarkUnaccountedPhysicalSlotAsFree(m_temporarySlotsToFreeLater[i]);
            }
        }
    };

    SetupResultForNodeWithoutRegAlloc WARN_UNUSED PrepareCodegenNodeWithoutRegAlloc(const DfgVariantTraits* trait,
                                                                                    std::span<ValueUseRAInfo> operands,
                                                                                    ValueRegAllocInfo* outputVal,
                                                                                    ValueRegAllocInfo* brDecisionOutputVal)
    {
        TestAssert(trait != nullptr);
        TestAssert(!trait->IsRegAllocEnabled());

        SetupResultForNodeWithoutRegAlloc r;
        r.m_numOperands = SafeIntegerCast<uint8_t>(operands.size());
        r.m_cgFnOrd = trait->GetCodegenFuncOrdNoRegAlloc();
        r.m_outputVal = outputVal;
        r.m_brDecisionOutputVal = brDecisionOutputVal;

        TestAssert(operands.size() < 32);

#ifdef TESTBUILD
        for (size_t i = 0; i < operands.size(); i++)
        {
            r.m_physicalSlotForOperands[i] = static_cast<uint16_t>(-1);
        }
#endif

        // For each operand that is a constant-like value, we must allocate a spill slot,
        // materialize the constant if it is not already in a register, and store the constant there
        //
        r.m_numTemporarySlotsToFree = 0;
        bool hasDuplicateEdgeInConstants = false;
        for (size_t i = 0; i < operands.size(); i++)
        {
            ValueUseRAInfo op = operands[i];
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            m_manager.AssertValueLiveAndRegInfoOK(ssaVal, true /*constantMaybeUnmaterialized*/);
            TestAssertImp(op.IsGPRUse(), ssaVal->GetGprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
            TestAssertImp(!op.IsGPRUse(), ssaVal->GetFprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
            if (ssaVal->IsConstantLikeNode())
            {
                if (op.IsDuplicateEdge())
                {
                    hasDuplicateEdgeInConstants = true;
                }
                else
                {
                    uint16_t spillSlot = m_manager.AllocateSpillSlotForTemporaryOrSSABornOnStack();
                    r.m_temporarySlotsToFreeLater[r.m_numTemporarySlotsToFree] = spillSlot;
                    r.m_numTemporarySlotsToFree++;
                    TestAssert(r.m_physicalSlotForOperands[i] == static_cast<uint16_t>(-1));
                    r.m_physicalSlotForOperands[i] = spillSlot;
                    if (ssaVal->IsAvailableInGPR())
                    {
                        CodegenLog().EmitRegSpill(ssaVal->GetGprRegister(), spillSlot);
                    }
                    else if (ssaVal->IsAvailableInFPR())
                    {
                        CodegenLog().EmitRegSpill(ssaVal->GetFprRegister(), spillSlot);
                    }
                    else
                    {
                        m_manager.MaterializeConstantToTempReg(ssaVal);
                        CodegenLog().EmitRegSpill(x_dfg_custom_purpose_temp_reg, spillSlot);
                    }
                }
            }
        }
        if (unlikely(hasDuplicateEdgeInConstants))
        {
            for (size_t i = 0; i < operands.size(); i++)
            {
                ValueUseRAInfo op = operands[i];
                ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                if (op.IsDuplicateEdge() && ssaVal->IsConstantLikeNode())
                {
                    TestAssert(r.m_physicalSlotForOperands[i] == static_cast<uint16_t>(-1));
                    for (size_t k = 0; k < operands.size(); k++)
                    {
                        ValueUseRAInfo opk = operands[k];
                        if (!opk.IsDuplicateEdge() && opk.GetSSAValue() == ssaVal)
                        {
                            TestAssert(r.m_physicalSlotForOperands[k] != static_cast<uint16_t>(-1));
                            r.m_physicalSlotForOperands[i] = r.m_physicalSlotForOperands[k];
                            break;
                        }
                    }
                    TestAssert(r.m_physicalSlotForOperands[i] != static_cast<uint16_t>(-1));
                }
            }
        }

        // Spill everything in register
        //
        m_gprAlloc.SpillEverything();
        m_fprAlloc.SpillEverything();

        m_gprAlloc.AssertAllRegistersScratched();
        m_fprAlloc.AssertAllRegistersScratched();
        m_manager.AssertAllRegistersScratched();

        // Set up physical slots for the non-constant-like operands (which must have been spilled),
        // and update NextUse for every operand
        //
        for (size_t i = 0; i < operands.size(); i++)
        {
            ValueUseRAInfo op = operands[i];
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            m_manager.AssertValueLiveAndRegInfoOK(ssaVal);
            TestAssert(!ssaVal->IsAvailableInGPR() && !ssaVal->IsAvailableInFPR());
            if (!ssaVal->IsConstantLikeNode())
            {
                TestAssert(ssaVal->IsSpilled());
                TestAssert(r.m_physicalSlotForOperands[i] == static_cast<uint16_t>(-1));
                r.m_physicalSlotForOperands[i] = ssaVal->GetPhysicalSpillSlot();
            }
            TestAssert(r.m_physicalSlotForOperands[i] != static_cast<uint16_t>(-1));

            // Update NextUse info
            //
            if (!op.IsDuplicateEdge())
            {
                TestAssert(op.GetNextUseInfo().GetNextUseIndex() > m_currentUseIndex);
                if (op.IsGPRUse())
                {
                    ssaVal->SetGprNextUseInfo(op.GetNextUseInfo());
                }
                else
                {
                    ssaVal->SetFprNextUseInfo(op.GetNextUseInfo());
                }
            }
        }

        r.m_outputPhysicalSlot = static_cast<uint16_t>(-1);
        if (outputVal != nullptr)
        {
            r.m_outputPhysicalSlot = m_manager.AllocateSpillSlotForTemporaryOrSSABornOnStack();
        }

        r.m_brDecisionPhysicalSlot = static_cast<uint16_t>(-1);
        if (brDecisionOutputVal != nullptr)
        {
            r.m_brDecisionPhysicalSlot = m_manager.AllocateSpillSlotForTemporaryOrSSABornOnStack();
        }
        return r;
    }

    // Similar to EmitMainLogicWithRegAlloc but for nodes that do not support reg alloc
    //
    void EmitMainLogicWithoutRegAlloc(Node* node, NodeRegAllocInfo* nodeInfo, uint16_t rangeOperandPhysicalSlot, const DfgVariantTraits* trait)
    {
        m_gprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        m_fprAlloc.AssertMinimumUseIndex(m_currentUseIndex);

        TestAssert(m_currentUseIndex == *m_nextSpillEverythingUseIndex);

        TestAssert(trait != nullptr);
        TestAssert(!trait->IsRegAllocEnabled());

        SetupResultForNodeWithoutRegAlloc cgInfo;
        {
            ValueRegAllocInfo* outputVal = nullptr;
            if (node->HasDirectOutput())
            {
                outputVal = node->GetValueRegAllocInfo();
                TestAssert(outputVal != nullptr);
            }
            ValueRegAllocInfo* brDecisionOutputVal = nullptr;
            if (node == m_nodeOutputtingBranchDecision)
            {
                brDecisionOutputVal = m_valueUseListBuilder.m_brDecision;
                TestAssert(brDecisionOutputVal != nullptr);
            }
            cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                       nodeInfo->GetFixedOperands(),
                                                       outputVal,
                                                       brDecisionOutputVal);
        }

        CodegenLog().EmitCodegenOpWithoutRegAlloc(
            nodeInfo->m_numFixedSSAInputs,
            [&](CodegenOpRegAllocDisabled* info) ALWAYS_INLINE
            {
                info->m_nsd = node->GetNodeSpecificDataOrNullptr();
                cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, rangeOperandPhysicalSlot);
            });

        cgInfo.UpdateStateAfterCodegen(m_manager);
    }

    // Load each operand in the list into register and process it using 'func', while attempting to minimize spills.
    // 1. 'func' should have prototype void(uint16_t ordInList, X64Reg reg), which means the value of opList[ordInList] is now in reg
    // 2. 'func' must not change any register state, including x_dfg_custom_purpose_temp_reg.
    // 3. 'func' will be called for each operand in the list, but there is no guarantee on the order of the operands.
    //
    // If 'shouldLockOneReg' is set to true, 'regToLock' should be set to a reg participating in reg alloc, and it will not be touched by reg alloc.
    //
    void ProcessRangedOperands(std::span<ValueUseRAInfo> opList,
                               FunctionRef<void(uint16_t /*ordInList*/, X64Reg /*reg*/)> func,
                               bool shouldLockOneReg = false,
                               X64Reg regToLock = X64Reg::RAX)
    {
        uint16_t numRangeInputs = SafeIntegerCast<uint16_t>(opList.size());
        if (numRangeInputs > 0)
        {
            m_gprAlloc.PrepareToProcessRangedOperands(numRangeInputs);
            m_fprAlloc.PrepareToProcessRangedOperands(numRangeInputs);

#ifdef TESTBUILD
            // Assert that the current NextUseIndex of all the SSA values should exactly be useIndex
            //
            for (ValueUseRAInfo use : opList)
            {
                if (use.IsGPRUse())
                {
                    TestAssert(use.GetSSAValue()->GetGprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
                }
                else
                {
                    TestAssert(use.GetSSAValue()->GetFprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
                }
            }

            // Assert that we did not miss to process a ranged operand
            //
            TempVector<bool> rangeOperandProcessed(m_bbAlloc);
            rangeOperandProcessed.resize(numRangeInputs, false /*value*/);
#endif

            bool hasPendingGprOps = false;
            bool hasPendingFprOps = false;
            for (uint16_t idx = 0; idx < numRangeInputs; idx++)
            {
                ValueUseRAInfo op = opList[idx];
                ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                m_manager.AssertValueLiveAndRegInfoOK(ssaVal, true /*constantMaybeUnmaterialized*/);

                // If the operand is already in the register, we can directly spill it to our desired location, and we are done.
                //
                if (ssaVal->IsAvailableInGPR() || ssaVal->IsAvailableInFPR())
                {
                    if (ssaVal->IsAvailableInGPR())
                    {
                        X64Reg reg = ssaVal->GetGprRegister();
                        func(idx, reg);
                    }
                    else
                    {
                        X64Reg reg = ssaVal->GetFprRegister();
                        func(idx, reg);
                    }
                    // We are done with the value, update use
                    //
                    if (!op.IsDuplicateEdge())
                    {
                        if (op.IsGPRUse())
                        {
                            ssaVal->SetGprNextUseInfo(op.GetNextUseInfo());
                            if (ssaVal->IsAvailableInGPR())
                            {
                                m_gprAlloc.UpdateNextUseInfo(ssaVal->GetGprOrdInList(), op);
                            }
                        }
                        else
                        {
                            ssaVal->SetFprNextUseInfo(op.GetNextUseInfo());
                            if (ssaVal->IsAvailableInFPR())
                            {
                                m_fprAlloc.UpdateNextUseInfo(ssaVal->GetFprOrdInList(), op);
                            }
                        }
                    }
#ifdef TESTBUILD
                    TestAssert(!rangeOperandProcessed[idx]);
                    rangeOperandProcessed[idx] = true;
#endif
                }
                else
                {
                    // The value is not in register, pass it to the reg alloc to deal with it (which is also responsible for updating NextUse)
                    //
                    if (op.IsGPRUse())
                    {
                        m_gprAlloc.AddNewRangedOperand(idx, op);
                        hasPendingGprOps = true;
                    }
                    else
                    {
                        m_fprAlloc.AddNewRangedOperand(idx, op);
                        hasPendingFprOps = true;
                    }
                }
            }

            // Populate the remaining operands into the range
            //
            auto processRemainingOperands = [&](std::span<uint16_t> list) ALWAYS_INLINE
            {
                ValueRegAllocInfo* lastValue = nullptr;
                X64Reg lastReg = x_dfg_custom_purpose_temp_reg;  // doesn't matter
                for (uint16_t ord : list)
                {
#ifdef TESTBUILD
                    TestAssert(ord < rangeOperandProcessed.size());
                    TestAssert(!rangeOperandProcessed[ord]);
                    rangeOperandProcessed[ord] = true;
#endif

                    ValueUseRAInfo op = opList[ord];
                    ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                    TestAssert(ssaVal != nullptr);

                    TestAssertImp(op.IsDuplicateEdge(), ssaVal == lastValue);

                    if (ssaVal == lastValue)
                    {
                        // The operand is already in lastReg
                        //
                        func(ord, lastReg);
                    }
                    else
                    {
                        X64Reg loadedReg;
                        // Check if the operand is already available in a reg (loaded by reg alloc), if so, we are good
                        //
                        if (ssaVal->IsAvailableInGPR())
                        {
                            loadedReg = ssaVal->GetGprRegister();
                        }
                        else if (ssaVal->IsAvailableInFPR())
                        {
                            loadedReg = ssaVal->GetFprRegister();
                        }
                        else
                        {
                            // We must emit logic to load this value into the temporary register
                            // Note that it is critical that we must not disturb the register allocation state,
                            // so we must load into x_dfg_custom_purpose_temp_reg
                            // (and EmitMaterializeConstantLikeNodeToTempReg is also designed to not clobber reg alloc state).
                            //
                            loadedReg = x_dfg_custom_purpose_temp_reg;
                            if (ssaVal->IsConstantLikeNode())
                            {
                                m_manager.MaterializeConstantToTempReg(ssaVal);
                            }
                            else
                            {
                                // This value must be available on the stack
                                //
                                TestAssert(ssaVal->IsSpilled());
                                uint16_t spillSlot = ssaVal->GetPhysicalSpillSlot();
                                CodegenLog().EmitRegLoad(spillSlot, x_dfg_custom_purpose_temp_reg);
                            }
                        }
                        // Now the value is available in 'loadedReg'
                        //
                        func(ord, loadedReg);

                        lastValue = ssaVal;
                        lastReg = loadedReg;
                    }
                }
            };

            uint8_t gprLockRegIdx = static_cast<uint8_t>(-1);
            uint8_t fprLockRegIdx = static_cast<uint8_t>(-1);
            if (shouldLockOneReg)
            {
                TestAssert(IsRegisterUsedForDfgRegAllocation(regToLock));
                size_t regSeqOrd = GetDfgRegAllocSequenceOrdForReg(regToLock);
                if (regToLock.IsGPR())
                {
                    TestAssert(regSeqOrd < x_dfg_reg_alloc_num_gprs);
                    gprLockRegIdx = SafeIntegerCast<uint8_t>(regSeqOrd);
                }
                else
                {
                    TestAssert(x_dfg_reg_alloc_num_gprs <= regSeqOrd && regSeqOrd < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
                    fprLockRegIdx = SafeIntegerCast<uint8_t>(regSeqOrd - x_dfg_reg_alloc_num_gprs);
                }
            }

            std::span<uint16_t> gprProcessOrder;
            std::span<uint16_t> fprProcessOrder;

            // First call ProcessRangedOperands on both register banks (which will emit logic that does the register operations)
            //
            if (hasPendingGprOps)
            {
                gprProcessOrder = m_gprAlloc.ProcessRangedOperands(gprLockRegIdx);
            }
            if (hasPendingFprOps)
            {
                fprProcessOrder = m_fprAlloc.ProcessRangedOperands(fprLockRegIdx);
            }

            // Then call the logic that process each operand
            //
            if (hasPendingGprOps)
            {
                processRemainingOperands(gprProcessOrder);
            }
            if (hasPendingFprOps)
            {
                processRemainingOperands(fprProcessOrder);
            }

#ifdef TESTBUILD
            // Validate post-condition: all input operands should have been processed, and NextUse updated
            //
            TestAssert(rangeOperandProcessed.size() == numRangeInputs);
            for (uint16_t idx = 0; idx < numRangeInputs; idx++)
            {
                TestAssert(rangeOperandProcessed[idx]);
                ValueUseRAInfo op = opList[idx];
                ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                TestAssert(ssaVal->GetGprNextUseInfo().GetNextUseIndex() > m_currentUseIndex);
                TestAssert(ssaVal->GetFprNextUseInfo().GetNextUseIndex() > m_currentUseIndex);
                m_manager.AssertValueLiveAndRegInfoOK(ssaVal);
                if (!op.IsDuplicateEdge())
                {
                    if (op.IsGPRUse())
                    {
                        TestAssert(ssaVal->GetGprNextUseInfo().GetNextUseIndex() == op.GetNextUseInfo().GetNextUseIndex());
                        TestAssert(ssaVal->GetGprNextUseInfo().IsAllFutureUseGhostUse() == op.GetNextUseInfo().IsAllFutureUseGhostUse());
                    }
                    else
                    {
                        TestAssert(ssaVal->GetFprNextUseInfo().GetNextUseIndex() == op.GetNextUseInfo().GetNextUseIndex());
                        TestAssert(ssaVal->GetFprNextUseInfo().IsAllFutureUseGhostUse() == op.GetNextUseInfo().IsAllFutureUseGhostUse());
                    }
                }
            }
#endif
        }
    }

    // Return the total number of spill slots needed to spill all the registers at this moment.
    //
    size_t WARN_UNUSED GetNumSlotsNeededToSpillAllRegisters()
    {
        size_t numSpillSlotsNeeded = 0;

        // Each non-constant-like SSA value that is not yet spilled needs a spill slot
        //
        m_gprAlloc.ForEachNonScratchRegister(
            [&]([[maybe_unused]] uint8_t regIdx, ValueRegAllocInfo* ssaVal) ALWAYS_INLINE
            {
                TestAssert(!ssaVal->HasNoMoreUseInBothGprAndFpr());
                TestAssert(ssaVal->GetGprOrdInList() == regIdx);
                if (ssaVal->IsNonConstantAndNotSpilled())
                {
                    numSpillSlotsNeeded++;
                }
            });

        m_fprAlloc.ForEachNonScratchRegister(
            [&]([[maybe_unused]] uint8_t regIdx, ValueRegAllocInfo* ssaVal) ALWAYS_INLINE
            {
                TestAssert(!ssaVal->HasNoMoreUseInBothGprAndFpr());
                TestAssert(ssaVal->GetFprOrdInList() == regIdx);
                if (ssaVal->IsNonConstantAndNotSpilled())
                {
                    // If this value is also available in GPR, it has been accounted for in the loop above
                    //
                    if (!ssaVal->IsAvailableInGPR())
                    {
                        numSpillSlotsNeeded++;
                    }
                }
            });

        return numSpillSlotsNeeded;
    }

    // Allocate a temp SSA value that has no use, only to keep the register manager happy
    // Note that since it has no use, it will be top candidate for eviction and will not even be spilled (since it has no more use!).
    // If this is not desired, caller logic should set up uses.
    //
    ValueRegAllocInfo* WARN_UNUSED AllocateTempOutputValue()
    {
        return DfgAlloc()->AllocateObject<ValueRegAllocInfo>();
    }

    void DeallocateTempOutputValue(ValueRegAllocInfo* ssaVal)
    {
        TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        ProcessDeathEvent(ssaVal);
    }

    // Codegen Return built-in node
    //
    void ProcessReturn(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(node->IsReturnNode());
        TestAssert(nodeInfo->GetRangedOperands().empty());
        AdvanceCurrentUseIndex();

        EmitAllTypeChecks(node, nodeInfo);

        TestAssert(nodeInfo->GetFixedOperands().size() == node->GetNumInputs());

        // This is ugly: we could have stored the return values at the end of the frame,
        // but we don't know the frame size until we have generated everything.
        // Of course we can leave all sorts of placeholders and patch them at the end, but it's complex.
        //
        // Instead, we store the return values at the *current* end of the frame.
        // Since there is no more logic after return, we can safely use the current stack frame length as the total number of slots,
        // without worrying about the values getting overwritten by later logic (since we are the last node executed).
        //
        // However, this still introduces an issue similar to the range operand issue:
        // we musr reserve in advance slots for everything we may spill, or the spill logic may grow the frame and accidentally overwrite stuffs.
        //
        size_t numSpillSlotsToReserve = GetNumSlotsNeededToSpillAllRegisters();

        // In addition to all the registers, we also have an internal temporary SSA value.
        // I don't think it will be spilled, but better safe than sorry.
        //
        numSpillSlotsToReserve++;

        m_manager.ReserveSpillSlots(numSpillSlotsToReserve);

        // Use AllocatePhysicalRange to set the flag that triggers assertion on any future stack frame growth
        //
        size_t totalNumberOfSlotsNow = m_manager.AllocatePhysicalRange(0 /*numDesiredSlots*/);

        // If true, this is a return consisting of a fix part + variadic results.
        // If false, this is a return with fixed number of results
        //
        if (node->IsNodeAccessesVR())
        {
            // Move the variadic result if needed, and append nils as needed to satisfy our protocol on return values
            //
            ValueRegAllocInfo* retStartSSAVal = AllocateTempOutputValue();

            {
                const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_MoveVariadicRes);
                TestAssert(trait->IsRegAllocEnabled());
                TestAssert(trait->NumOperandsForRA() == 0 && trait->HasOutput() && !trait->Output().HasChoices());

                RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                                      std::span<ValueUseRAInfo>{} /*operands*/,
                                                                      retStartSSAVal /*outputVal*/,
                                                                      true /*outputShouldUseGpr*/,
                                                                      nullptr /*brDecisionOutputVal*/);

                CodegenLog().EmitCodegenCustomOpWithRegAlloc(
                    0 /*numInputOperands*/,
                    [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
                    {
                        // Return_MoveVariadicRes takes three literal configurations:
                        // [0]: the number of fixed return values
                        // [1]: the total number of slots in the frame
                        // [2]: sum of [0] and [1]
                        //
                        info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(3);
                        info->m_literalData[0] = nodeInfo->GetFixedOperands().size();
                        info->m_literalData[1] = totalNumberOfSlotsNow;
                        info->m_literalData[2] = info->m_literalData[0] + info->m_literalData[1];

                        rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                                      info->m_operandConfig /*out*/,
                                                      m_firstRegSpillSlot,
                                                      static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                    });

                rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);
            }

            TestAssert(retStartSSAVal->IsAvailableInGPR());
            X64Reg retStartReg = retStartSSAVal->GetGprRegister();

            // Store each fixed-part return value into the return value list
            //
            ProcessRangedOperands(
                nodeInfo->GetFixedOperands(),
                [&](uint16_t idx, X64Reg reg)
                {
                    TestAssert(idx < nodeInfo->GetFixedOperands().size());
                    uint32_t byteOffset = static_cast<uint32_t>(idx * sizeof(TValue));
                    CodegenLog().EmitRegStoreToBaseOffsetBytes(reg /*srcReg*/,
                                                               retStartReg /*baseReg*/,
                                                               byteOffset);
                },
                true /*shouldLockOneReg*/,
                retStartReg /*regToLock*/);

            TestAssert(retStartSSAVal->IsAvailableInGPR());
            TestAssert(retStartSSAVal->GetGprRegister() == retStartReg);

            DeallocateTempOutputValue(retStartSSAVal);

            // Emit the final return logic
            //
            {
                const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_RetWithVariadicRes);
                TestAssert(!trait->IsRegAllocEnabled());
                SetupResultForNodeWithoutRegAlloc cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                                                             std::span<ValueUseRAInfo>{} /*operands*/,
                                                                                             nullptr /*outputVal*/,
                                                                                             nullptr /*brDecisionVal*/);

                CodegenLog().EmitCodegenCustomOpWithoutRegAlloc(
                    0 /*numInputOperands*/,
                    [&](CodegenCustomOpRegAllocDisabled* info) ALWAYS_INLINE
                    {
                        info->m_literalData = nullptr;
                        cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                    });

                cgInfo.UpdateStateAfterCodegen(m_manager);
            }
        }
        else
        {
            // This is a return with a fixed number of results
            //
            // Store all the return values first
            //
            std::span<ValueUseRAInfo> operands = nodeInfo->GetFixedOperands();

            ProcessRangedOperands(
                operands,
                [&](uint16_t idx, X64Reg reg)
                {
                    size_t slotOrd = totalNumberOfSlotsNow + idx;
                    CodegenLog().EmitRegSpill(reg, SafeIntegerCast<uint16_t>(slotOrd));
                });

            // Now, emit the final return logic.
            // We have specializations for the Ret0 and Ret1 common case
            //
            if (operands.size() == 0)
            {
                const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_Ret0);
                TestAssert(!trait->IsRegAllocEnabled());
                SetupResultForNodeWithoutRegAlloc cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                                                             std::span<ValueUseRAInfo>{} /*operands*/,
                                                                                             nullptr /*outputVal*/,
                                                                                             nullptr /*brDecisionVal*/);

                CodegenLog().EmitCodegenCustomOpWithoutRegAlloc(
                    0 /*numInputOperands*/,
                    [&](CodegenCustomOpRegAllocDisabled* info) ALWAYS_INLINE
                    {
                        info->m_literalData = nullptr;
                        cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                    });

                cgInfo.UpdateStateAfterCodegen(m_manager);
            }
            else if (operands.size() == 1)
            {
                const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_Ret1);
                TestAssert(!trait->IsRegAllocEnabled());
                SetupResultForNodeWithoutRegAlloc cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                                                             std::span<ValueUseRAInfo>{} /*operands*/,
                                                                                             nullptr /*outputVal*/,
                                                                                             nullptr /*brDecisionVal*/);

                CodegenLog().EmitCodegenCustomOpWithoutRegAlloc(
                    0 /*numInputOperands*/,
                    [&](CodegenCustomOpRegAllocDisabled* info) ALWAYS_INLINE
                    {
                        // One literal configuration: where the return value is
                        //
                        info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(1);
                        info->m_literalData[0] = totalNumberOfSlotsNow;
                        cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                    });

                cgInfo.UpdateStateAfterCodegen(m_manager);
            }
            else
            {
                // We may need to append nils to the end of the return values to satisfy our internal protocol
                // Note that for Ret0 and Ret1, the specialized implementation has already dealt with that for us.
                //
                if (operands.size() < x_minNilFillReturnValues)
                {
                    size_t numNilFillsNeeded = x_minNilFillReturnValues - operands.size();
                    for (size_t idx = 0; idx < numNilFillsNeeded; idx++)
                    {
                        const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_WriteNil);
                        TestAssert(trait->IsRegAllocEnabled());
                        TestAssert(trait->NumOperandsForRA() == 0 && !trait->HasOutput());

                        RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                                              std::span<ValueUseRAInfo>{} /*operands*/,
                                                                              nullptr /*outputVal*/,
                                                                              true /*outputShouldUseGpr*/,
                                                                              nullptr /*brDecisionOutputVal*/);

                        CodegenLog().EmitCodegenCustomOpWithRegAlloc(
                            0 /*numInputOperands*/,
                            [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
                            {
                                // One literal configuration:
                                // [0]: the slot number to write nil
                                //
                                info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(1);
                                info->m_literalData[0] = totalNumberOfSlotsNow + operands.size() + idx;

                                rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                                              info->m_operandConfig /*out*/,
                                                              m_firstRegSpillSlot,
                                                              static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                            });

                        rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);
                    }
                }

                // Now, emit the final return logic
                //
                {
                    const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::Return_RetNoVariadicRes);
                    TestAssert(!trait->IsRegAllocEnabled());
                    SetupResultForNodeWithoutRegAlloc cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                                                                 std::span<ValueUseRAInfo>{} /*operands*/,
                                                                                                 nullptr /*outputVal*/,
                                                                                                 nullptr /*brDecisionVal*/);

                    CodegenLog().EmitCodegenCustomOpWithoutRegAlloc(
                        0 /*numInputOperands*/,
                        [&](CodegenCustomOpRegAllocDisabled* info) ALWAYS_INLINE
                        {
                            // Two literal configurations:
                            // [0]: slotOrd for return value start
                            // [1]: number of return values
                            //
                            info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(2);
                            info->m_literalData[0] = totalNumberOfSlotsNow;
                            info->m_literalData[1] = operands.size();
                            cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                        });

                    cgInfo.UpdateStateAfterCodegen(m_manager);
                }
            }
        }

        // Deallocate the dummy physical slot range, which also asserts that the total number
        // of slots has not grown higher after we generate the node
        //
        m_manager.ShrinkPhysicalFrameLength(SafeIntegerCast<uint16_t>(totalNumberOfSlotsNow) /*expectedFrameLength*/,
                                            SafeIntegerCast<uint16_t>(totalNumberOfSlotsNow) /*newFrameLength*/);

        AdvanceCurrentUseIndex();
    }

    // Codegen CreateFunctionObject built-in node
    //
    void ProcessCreateFunctionObject(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(node->IsCreateFunctionObjectNode());
        TestAssert(nodeInfo->GetRangedOperands().empty());
        AdvanceCurrentUseIndex();

        EmitAllTypeChecks(node, nodeInfo);

        TestAssert(node->GetNumInputs() == nodeInfo->GetFixedOperands().size());
        TestAssert(nodeInfo->GetFixedOperands().size() >= 2);

        TestAssert(m_currentUseIndex == *m_nextSpillEverythingUseIndex);

        ValueRegAllocInfo* tempSSAVal = nullptr;
        ValueUseRAInfo useForTempVal;
        {
            const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::CreateFunctionObject_AllocAndSetup);
            std::span<ValueUseRAInfo> operands { nodeInfo->GetFixedOperands().data(), 2 /*numElements*/ };

            // Note that the output is hackily stored into x_dfg_reg_alloc_gprs[0], so no output on paper
            //
            SetupResultForNodeWithoutRegAlloc cgInfo = PrepareCodegenNodeWithoutRegAlloc(trait,
                                                                                         operands,
                                                                                         nullptr /*outputVal*/,
                                                                                         nullptr /*brDecisionOutputVal*/);

            CodegenLog().EmitCodegenCustomOpWithoutRegAlloc(
                2 /*numInputOperands*/,
                [&](CodegenCustomOpRegAllocDisabled* info) ALWAYS_INLINE
                {
                    info->m_literalData = nullptr;
                    cgInfo.SetupCodegenInfo(info->m_operandConfig /*out*/, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                });

            cgInfo.UpdateStateAfterCodegen(m_manager);

            // Make the output born in x_dfg_reg_alloc_gprs[0]
            // We must also set up a use for it, since it needs to be eventually used as the operand of another logic fragment
            //
            tempSSAVal = AllocateTempOutputValue();
            useForTempVal.Initialize(tempSSAVal, m_currentUseIndex, true /*isGprUse*/, false /*isGhostLikeUse*/, true /*doNotProduceDuplicateEdge*/);

            m_gprAlloc.ProcessHackyBorn(0 /*regIdx*/, tempSSAVal);
            m_manager.ProcessBornInRegister<true /*forGprState*/>(tempSSAVal, 0 /*regIdx*/);
            m_gprAlloc.ClearOutputRegMaskAfterBornEvents();
        }

        X64Reg fnObjReg = x_dfg_reg_alloc_gprs[0];
        TestAssert(tempSSAVal->IsAvailableInGPR());
        TestAssert(tempSSAVal->GetGprRegister() == fnObjReg);

        TestAssert(node->GetInputEdge(1).GetOperand()->IsUnboxedConstantNode());
        UnlinkedCodeBlock* ucb = reinterpret_cast<UnlinkedCodeBlock*>(node->GetInputEdge(1).GetOperand()->GetUnboxedConstantNodeValue());

        std::span<ValueUseRAInfo> parentLocalVals { nodeInfo->GetFixedOperands().data() + 2, nodeInfo->GetFixedOperands().size() - 2 };

        // Figure out the mapping of each parentLocalVal to the index into the upvalue list
        //
        GrowVectorToAtLeast(m_createFnObjUvIndexList, parentLocalVals.size(), 0U /*value*/);
        uint32_t* uvOrdList = m_createFnObjUvIndexList.data();
        size_t selfReferenceUvOrd = node->GetNodeSpecificDataAsUInt64();
        TestAssertImp(selfReferenceUvOrd != static_cast<size_t>(-1), selfReferenceUvOrd < ucb->m_numUpvalues);
        TestAssertImp(selfReferenceUvOrd != static_cast<size_t>(-1),
                      ucb->m_upvalueInfo[selfReferenceUvOrd].m_isParentLocal && ucb->m_upvalueInfo[selfReferenceUvOrd].m_isImmutable);

        {
            size_t curIdx = 0;
            for (size_t uvOrd = 0; uvOrd < ucb->m_numUpvalues; uvOrd++)
            {
                UpvalueMetadata& uvmt = ucb->m_upvalueInfo[uvOrd];
                if (uvmt.m_isParentLocal)
                {
                    if (uvOrd != selfReferenceUvOrd)
                    {
                        TestAssert(curIdx < parentLocalVals.size());
                        uvOrdList[curIdx] = static_cast<uint32_t>(uvOrd);
                        curIdx++;
                    }
                }
            }
            TestAssert(curIdx == parentLocalVals.size());
        }

        // Populate each value into the upvalue list
        //
        ProcessRangedOperands(
            parentLocalVals,
            [&](uint16_t idx, X64Reg reg)
            {
                TestAssert(idx < parentLocalVals.size());
                uint32_t uvOrd = uvOrdList[idx];
                uint32_t byteOffset = static_cast<uint32_t>(FunctionObject::GetUpvalueAddrByteOffsetFromThisPointer(uvOrd));
                CodegenLog().EmitRegStoreToBaseOffsetBytes(reg /*srcReg*/,
                                                           fnObjReg /*baseReg*/,
                                                           byteOffset);
            },
            true /*shouldLockOneReg*/,
            fnObjReg /*regToLock*/);

        TestAssert(tempSSAVal->IsAvailableInGPR());
        TestAssert(tempSSAVal->GetGprRegister() == fnObjReg);

        TestAssert(node->HasDirectOutput());
        TestAssert(node->ShouldOutputRegisterBankUseGPR());
        ValueRegAllocInfo* outputVal = node->GetValueRegAllocInfo();

        if (selfReferenceUvOrd != static_cast<size_t>(-1))
        {
            // There is a self-reference in upvalue, we need to box the function and then populate its value
            //
            uint64_t selfRefUvByteOffset = FunctionObject::GetUpvalueAddrByteOffsetFromThisPointer(selfReferenceUvOrd);

            const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::CreateFunctionObject_BoxFnObjAndWriteSelfRefUv);

            std::span<ValueUseRAInfo> operands { &useForTempVal, 1 /*numElements*/ };
            RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                                  operands,
                                                                  outputVal,
                                                                  true /*outputShouldUseGpr*/,
                                                                  nullptr /*brDecisionOutputVal*/);

            CodegenLog().EmitCodegenCustomOpWithRegAlloc(
                1 /*numInputOperands*/,
                [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
                {
                    // One literal configuration: selfRefUvByteOffset
                    //
                    info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(1);
                    info->m_literalData[0] = selfRefUvByteOffset;

                    rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                                  info->m_operandConfig /*out*/,
                                                  m_firstRegSpillSlot,
                                                  static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                });

            rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);
        }
        else
        {
            // Just box the value and we are done
            //
            const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::CreateFunctionObject_BoxFunctionObject);

            std::span<ValueUseRAInfo> operands { &useForTempVal, 1 /*numElements*/ };
            RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                                  operands,
                                                                  outputVal,
                                                                  true /*outputShouldUseGpr*/,
                                                                  nullptr /*brDecisionOutputVal*/);

            CodegenLog().EmitCodegenCustomOpWithRegAlloc(
                1 /*numInputOperands*/,
                [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
                {
                    info->m_literalData = nullptr;
                    rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                                  info->m_operandConfig /*out*/,
                                                  m_firstRegSpillSlot,
                                                  static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                });

            rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);
        }

        TestAssert(tempSSAVal->HasNoMoreUseInBothGprAndFpr());
        DeallocateTempOutputValue(tempSSAVal);

        TestAssert(outputVal->IsAvailableInGPR());
        AdvanceCurrentUseIndex();
    }

    // Codegen CreateVariadicRes built-in node
    //
    void ProcessCreateVariadicRes(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(node->IsCreateVariadicResNode());
        TestAssert(nodeInfo->GetRangedOperands().empty());
        AdvanceCurrentUseIndex();

        EmitAllTypeChecks(node, nodeInfo);

        TestAssert(node->GetNumInputs() == nodeInfo->GetFixedOperands().size());
        TestAssert(nodeInfo->GetFixedOperands().size() >= 1);

        std::span<ValueUseRAInfo> numDynVarRes { nodeInfo->GetFixedOperands().data(), 1 /*numElements*/ };
        std::span<ValueUseRAInfo> valueList { nodeInfo->GetFixedOperands().data() + 1, nodeInfo->GetFixedOperands().size() - 1 };

        const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::CreateVariadicRes_StoreInfo);
        TestAssert(trait->IsRegAllocEnabled());
        TestAssert(trait->NumOperandsForRA() == 1 && trait->HasOutput() && !trait->Output().HasChoices());

        ValueRegAllocInfo* outputVal = AllocateTempOutputValue();

        RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                              numDynVarRes /*operands*/,
                                                              outputVal,
                                                              true /*outputShouldUseGpr*/,
                                                              nullptr /*brDecisionOutputVal*/);

        CodegenLog().EmitCodegenCustomOpWithRegAlloc(
            1 /*numInputOperands*/,
            [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
            {
                // CreateVariadicRes_StoreInfo takes 1 literal configuration:
                // [0]: the number of fixed VarRes
                //
                info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(1);
                info->m_literalData[0] = node->GetNodeSpecificDataAsUInt64();

                rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                              info->m_operandConfig /*out*/,
                                              m_firstRegSpillSlot,
                                              static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
            });

        rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);

        TestAssert(outputVal->IsAvailableInGPR());
        X64Reg vresStartReg = outputVal->GetGprRegister();

        // Store each input operand: operand i should be stored to vresStartReg[i]
        //
        ProcessRangedOperands(
            valueList,
            [&](uint16_t idx, X64Reg reg) ALWAYS_INLINE
            {
                CodegenLog().EmitRegStoreToBaseOffsetBytes(
                    reg /*srcReg*/,
                    vresStartReg /*baseReg*/,
                    static_cast<uint32_t>(idx * sizeof(TValue)) /*offsetBytes*/);
            },
            true /*shouldLockOneReg*/,
            vresStartReg /*regToLock*/);

        TestAssert(outputVal->IsAvailableInGPR());
        TestAssert(outputVal->GetGprRegister() == vresStartReg);

        DeallocateTempOutputValue(outputVal);
        AdvanceCurrentUseIndex();
    }

    // Codegen PrependVariadicRes built-in node
    //
    void ProcessPrependVariadicRes(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(node->IsPrependVariadicResNode());
        TestAssert(nodeInfo->GetRangedOperands().empty());
        AdvanceCurrentUseIndex();

        EmitAllTypeChecks(node, nodeInfo);

        TestAssert(node->GetNumInputs() == nodeInfo->GetFixedOperands().size());
        if (node->GetNumInputs() == 0)
        {
            // A PrependVariadicRes with no inputs is used only to indicate that the VariadicResults come from another BB,
            // and is a no-op. So nothing to generate.
            //
            TestAssert(nodeInfo->GetCheckOperands().size() == 0);
            AdvanceCurrentUseIndex();
        }
        else
        {
            const DfgVariantTraits* trait = GetCodegenInfoForCustomBuiltinNodeLogicFragment(DfgBuiltinNodeCustomCgFn::PrependVariadicRes_MoveAndStoreInfo);
            TestAssert(trait->IsRegAllocEnabled());
            TestAssert(trait->NumOperandsForRA() == 0 && trait->HasOutput() && !trait->Output().HasChoices());

            ValueRegAllocInfo* outputVal = AllocateTempOutputValue();

            RegConfigResult rcInfo = ConfigureRegistersForCodegen(trait,
                                                                  std::span<ValueUseRAInfo>{} /*operands*/,
                                                                  outputVal,
                                                                  true /*outputShouldUseGpr*/,
                                                                  nullptr /*brDecisionOutputVal*/);

            CodegenLog().EmitCodegenCustomOpWithRegAlloc(
                0 /*numInputOperands*/,
                [&](CodegenCustomOpRegAllocEnabled* info) ALWAYS_INLINE
                {
                    // PrependVariadicRes_MoveAndStoreInfo takes three literal configurations:
                    // [0]: the number of values prepended
                    // [1]: the total number of slots in the frame
                    // [2]: sum of [0] and [1]
                    //
                    // Unfortunately we only know the total number of slots in the frame after we generate everything,
                    // so we have to patch later.
                    //
                    info->m_literalData = m_passAlloc.AllocateArray<uint64_t>(3);
                    info->m_literalData[0] = nodeInfo->GetFixedOperands().size();
                    info->m_literalData[1] = 0;
                    info->m_literalData[2] = info->m_literalData[0];
                    m_literalFieldToBeAddedByTotalFrameSlots.push_back(info->m_literalData + 1);
                    m_literalFieldToBeAddedByTotalFrameSlots.push_back(info->m_literalData + 2);

                    rcInfo.SetupCodegenConfigInfo(info->m_regConfig /*out*/,
                                                  info->m_operandConfig /*out*/,
                                                  m_firstRegSpillSlot,
                                                  static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/);
                });

            rcInfo.UpdateStateAfterCodegen(m_manager, m_gprAlloc, m_fprAlloc);

            TestAssert(outputVal->IsAvailableInGPR());
            X64Reg vresStartReg = outputVal->GetGprRegister();

            // Store each input operand: operand i should be stored to vresStartReg[i]
            //
            ProcessRangedOperands(
                nodeInfo->GetFixedOperands(),
                [&](uint16_t idx, X64Reg reg) ALWAYS_INLINE
                {
                    CodegenLog().EmitRegStoreToBaseOffsetBytes(
                        reg /*srcReg*/,
                        vresStartReg /*baseReg*/,
                        static_cast<uint32_t>(idx * sizeof(TValue)) /*offsetBytes*/);
                },
                true /*shouldLockOneReg*/,
                vresStartReg /*regToLock*/);

            TestAssert(outputVal->IsAvailableInGPR());
            TestAssert(outputVal->GetGprRegister() == vresStartReg);

            DeallocateTempOutputValue(outputVal);
            AdvanceCurrentUseIndex();
        }
    }

    // Process ShadowStore/ShadowStoreUndefToRange/Phantom built-in node
    //
    void ProcessGhostLikeNode(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        NodeKind nodeKind = node->GetNodeKind();
        TestAssert(nodeKind == NodeKind_ShadowStore || nodeKind == NodeKind_ShadowStoreUndefToRange || nodeKind == NodeKind_Phantom);

        // The node must not have type check or range operand
        //
        TestAssert(nodeInfo->m_numChecks == 0);
        TestAssert(nodeInfo->m_numRangeSSAInputs == 0);

        // ShadowStore have 0 or 1 fixed operand, depending on if that is the last use of the SSA value
        //
        TestAssertImp(nodeKind == NodeKind_ShadowStore, nodeInfo->m_numFixedSSAInputs <= 1);
        TestAssertImp(nodeKind == NodeKind_ShadowStoreUndefToRange, nodeInfo->m_numFixedSSAInputs == 0);
        TestAssertImp(nodeKind == NodeKind_Phantom, nodeInfo->m_numFixedSSAInputs == 1);

        // For ShadowStore, we need to update the OSR exit map
        // Note that we don't need to update the OSR exit map for NodeKind_ShadowStoreUndefToRange, since it's storing Undef anyway
        //
        if (nodeKind == NodeKind_ShadowStore)
        {
            ValueRegAllocInfo* ssaVal = ValueUseListBuilder::GetValueRegAllocInfo(node->GetSoleInput());
            TestAssertImp(nodeInfo->m_numFixedSSAInputs == 1, nodeInfo->GetFixedOperands().front().GetSSAValue() == ssaVal);

            m_manager.ProcessShadowStore(ssaVal, node->GetShadowStoreInterpreterSlotOrd());
        }

        // Advance the useIndex for the (empty) ranged operand phase and check phase
        //
        AdvanceCurrentUseIndex();
        AdvanceCurrentUseIndex();

        // Update NextUse if needed
        //
        if (nodeInfo->m_numFixedSSAInputs > 0)
        {
            TestAssert(nodeInfo->m_numFixedSSAInputs == 1);
            ValueUseRAInfo op = nodeInfo->GetFixedOperands().front();
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            m_manager.AssertValueLiveAndRegInfoOK(ssaVal);

            TestAssert(!op.IsDuplicateEdge());
            // We must be the last use (in fact, the last use in both reg banks)
            //
            TestAssert(!op.GetNextUseInfo().HasNextUse());
            if (op.IsGPRUse())
            {
                TestAssert(ssaVal->GetGprNextUseInfo().IsAllFutureUseGhostUse());
                TestAssert(ssaVal->GetGprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
                ssaVal->SetGprNextUseInfo(op.GetNextUseInfo());
                if (ssaVal->IsAvailableInGPR())
                {
                    m_gprAlloc.UpdateNextUseInfo(ssaVal->GetGprOrdInList(), op);
                }
            }
            else
            {
                TestAssert(ssaVal->GetFprNextUseInfo().IsAllFutureUseGhostUse());
                TestAssert(ssaVal->GetFprNextUseInfo().GetNextUseIndex() == m_currentUseIndex);
                ssaVal->SetFprNextUseInfo(op.GetNextUseInfo());
                if (ssaVal->IsAvailableInFPR())
                {
                    m_fprAlloc.UpdateNextUseInfo(ssaVal->GetFprOrdInList(), op);
                }
            }
            // The SSA value must have no more use now
            //
            TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
            m_manager.AssertValueLiveAndRegInfoOK(ssaVal);
        }
        AdvanceCurrentUseIndex();
    }

    void ProcessBuiltinNode(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(node->IsBuiltinNodeKind());
        m_gprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        m_fprAlloc.AssertMinimumUseIndex(m_currentUseIndex);

        // For nodeKind that needs special handling, dispatch to the corresponding function and we are done.
        // This function only deals with node with fixed number of inputs/outputs and supports reg alloc
        //
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
        case NodeKind_Return:
        {
            ProcessReturn(node, nodeInfo);
            return;
        }
        case NodeKind_CreateFunctionObject:
        {
            ProcessCreateFunctionObject(node, nodeInfo);
            return;
        }
        case NodeKind_CreateVariadicRes:
        {
            ProcessCreateVariadicRes(node, nodeInfo);
            return;
        }
        case NodeKind_PrependVariadicRes:
        {
            ProcessPrependVariadicRes(node, nodeInfo);
            return;
        }
        case NodeKind_ShadowStore:
        case NodeKind_ShadowStoreUndefToRange:
        case NodeKind_Phantom:
        {
            ProcessGhostLikeNode(node, nodeInfo);
            return;
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
            // These nodes are standard nodes with fixed number of inputs/outputs
            // Note that SetLocal needs some special handling, but we will specially check for that
            //
            break;
        }
        case NodeKind_Nop:
        {
            // The Nop node must not have any non-check uses, we only need to codegen checks
            // Still, it can use our standard processing logic
            //
            TestAssert(nodeInfo->m_numFixedSSAInputs == 0 && nodeInfo->m_numRangeSSAInputs == 0);
            break;
        }
        }   /*switch*/

        // At this point, we know the node has no ranged input to deal with
        //
        TestAssert(nodeInfo->m_numRangeSSAInputs == 0);
        AdvanceCurrentUseIndex();

        // Codegen the typechecks
        //
        EmitAllTypeChecks(node, nodeInfo);

        // Codegen main node logic
        //
        if (nodeKind != NodeKind_Nop)
        {
            const DfgVariantTraits* trait = GetCodegenInfoForBuiltinNodeKind(nodeKind);

            // Deegen should have set up the codegen data for this node, and it should support reg alloc,
            // otherwise the logic here is having a discrepency with Deegen's build time logic
            //
            TestAssert(trait != nullptr);
            TestAssert(trait->IsRegAllocEnabled());
            EmitMainLogicWithRegAlloc(node, nodeInfo, static_cast<uint16_t>(-1) /*rangeOperandPhysicalSlot*/, trait, false /*shouldEmitRegConfigInSlowPathData*/);
        }
        AdvanceCurrentUseIndex();

        // For SetLocal, after the node executes, the OSR exit map needs to be updated correspondingly
        //
        if (nodeKind == NodeKind_SetLocal)
        {
            TestAssert(nodeInfo->m_numFixedSSAInputs == 1);
            ValueUseRAInfo op = nodeInfo->GetFixedOperands().front();
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            TestAssert(ssaVal == ValueUseListBuilder::GetValueRegAllocInfo(node->GetSoleInput()));

            size_t localPhysicalSlot = node->GetNodeSpecificDataAsUInt64();
            TestAssert(localPhysicalSlot != static_cast<size_t>(-1));
            TestAssert(GetGraph()->GetFirstDfgPhysicalSlotForLocal() <= localPhysicalSlot &&
                       localPhysicalSlot < GetGraph()->GetFirstPhysicalSlotForTemps());

            m_manager.ProcessSetLocal(ssaVal,
                                      node->GetLogicalVariable()->GetInterpreterSlot(),
                                      SafeIntegerCast<uint16_t>(localPhysicalSlot));
        }
    }

    void ProcessGuestLanguageNode(Node* node, NodeRegAllocInfo* nodeInfo)
    {
        TestAssert(!node->IsBuiltinNodeKind());
        m_gprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        m_fprAlloc.AssertMinimumUseIndex(m_currentUseIndex);
        TestAssert(!nodeInfo->m_isGhostLikeNode && !nodeInfo->m_isShadowStoreNode);

        // Decode the range operand of the node if it exists
        //
        m_rangeOpInfoDecoder.Query(node);
        TestAssert(nodeInfo->m_numRangeSSAInputs == m_rangeOpInfoDecoder.m_numInputs);
        TestAssertImp(nodeInfo->m_numRangeSSAInputs > 0, m_rangeOpInfoDecoder.m_nodeHasRangeOperand);

        BCKind bcKind = node->GetGuestLanguageBCKind();
        uint8_t dfgVariantOrd = node->DfgVariantOrd();
        const DfgVariantTraits* trait = GetCodegenInfoForDfgVariant(bcKind, dfgVariantOrd);
        bool supportsRegAlloc = trait->IsRegAllocEnabled();
        bool shouldEmitRegConfigInSlowPathData = DfgVariantNeedsRegConfigInSlowPathData(bcKind, dfgVariantOrd);
        TestAssertImp(!supportsRegAlloc, !shouldEmitRegConfigInSlowPathData);

        m_slowPathDataEndOffset += GetDfgVariantSlowPathDataLength(bcKind, dfgVariantOrd);

        // If the node has a range operand, we must reserve enough spill slots to hold everything that might be spilled
        // to the stack before executing this node.
        // This is because the range operand must sit at the end of the frame, so no allocation of new spill slots at the end
        // of the frame is allowed after the range operand is allocated.
        //
        if (m_rangeOpInfoDecoder.m_nodeHasRangeOperand)
        {
            // We reserve a slot for all the registers that haven't been spilled yet, conservatively assuming that it may be spilled
            //
            size_t numSpillSlotsNeeded = GetNumSlotsNeededToSpillAllRegisters();

            // If the node does not support reg alloc, all its operands and outputs need to be on the stack,
            // so we may need to reserve more slots
            //
            if (!supportsRegAlloc)
            {
                // We must allocate a temporary stack slot for every operand that is a constant.
                //
                for (ValueUseRAInfo op : nodeInfo->GetFixedOperands())
                {
                    if (op.GetSSAValue()->IsConstantLikeNode())
                    {
                        if (!op.IsDuplicateEdge())
                        {
                            numSpillSlotsNeeded++;
                        }
                    }
                }
                // If the node has output/brDecisionOutput, must reserve a slot for them as well
                //
                if (node->HasDirectOutput())
                {
                    numSpillSlotsNeeded++;
                }
                if (node == m_nodeOutputtingBranchDecision)
                {
                    numSpillSlotsNeeded++;
                }
            }
            m_manager.ReserveSpillSlots(numSpillSlotsNeeded);
        }

        // If the node has a range operand, allocate space for the operand on the stack, then populate all input operands into the range
        //
        uint16_t rangeStartPhysicalSlot = static_cast<uint16_t>(-1);
        if (m_rangeOpInfoDecoder.m_nodeHasRangeOperand)
        {
            // Allocate the physical range that holds the inputs and outputs
            //
            rangeStartPhysicalSlot = m_manager.AllocatePhysicalRange(SafeIntegerCast<uint16_t>(m_rangeOpInfoDecoder.m_requiredRangeSize));

            // If there are inputs in the ranged operand, we must populate them into the range now
            //
            if (nodeInfo->GetRangedOperands().size() > 0)
            {
                ProcessRangedOperands(
                    nodeInfo->GetRangedOperands(),
                    [&](uint16_t idx, X64Reg reg)
                    {
                        TestAssert(idx < nodeInfo->m_numRangeSSAInputs);
                        TestAssert(m_rangeOpInfoDecoder.m_inputOffsets[idx] < m_rangeOpInfoDecoder.m_requiredRangeSize);
                        uint16_t destPhysicalSlot = SafeIntegerCast<uint16_t>(rangeStartPhysicalSlot + m_rangeOpInfoDecoder.m_inputOffsets[idx]);
                        CodegenLog().EmitRegSpill(reg, destPhysicalSlot);
                    });
            }
        }
        else
        {
            TestAssert(nodeInfo->m_numRangeSSAInputs == 0);
        }
        AdvanceCurrentUseIndex();

        // Generate code for each check
        //
        EmitAllTypeChecks(node, nodeInfo);

        // Generate the main node logic
        // TODO FIXME: we need to generate the SlowPathRegConfigData if needed
        //
        if (supportsRegAlloc)
        {
            EmitMainLogicWithRegAlloc(node, nodeInfo, rangeStartPhysicalSlot, trait, shouldEmitRegConfigInSlowPathData);
        }
        else
        {
            EmitMainLogicWithoutRegAlloc(node, nodeInfo, rangeStartPhysicalSlot, trait);
        }

        // If the node has a range operand, we need to free the slots that are not holding an output,
        // and process the born events of the outputs in the range
        //
        if (m_rangeOpInfoDecoder.m_nodeHasRangeOperand)
        {
            size_t numOutputs = m_rangeOpInfoDecoder.m_numOutputs;
            TestAssert(node->GetNumExtraOutputs() == numOutputs);
            if (numOutputs > 0)
            {
                size_t outputUsedLen = 0;
                m_isRangeOpSlotHoldingOutput.clear();
                ResizeVectorTo(m_isRangeOpSlotHoldingOutput, m_rangeOpInfoDecoder.m_requiredRangeSize, static_cast<uint8_t>(0) /*initValue*/);
                ValueRegAllocInfo* outputSSAList = node->GetValueRegAllocInfo() + 1;
                for (size_t i = 0; i < numOutputs; i++)
                {
                    size_t offset = m_rangeOpInfoDecoder.m_outputOffsets[i];
                    TestAssert(offset < m_isRangeOpSlotHoldingOutput.size());
                    TestAssert(m_isRangeOpSlotHoldingOutput[offset] == 0);
                    m_isRangeOpSlotHoldingOutput[offset] = 1;
                    m_manager.ProcessBornOnStack(outputSSAList + i, static_cast<uint16_t>(rangeStartPhysicalSlot + offset));
                    outputUsedLen = std::max(outputUsedLen, offset + 1);
                }
                TestAssert(outputUsedLen <= m_rangeOpInfoDecoder.m_requiredRangeSize);
                m_manager.ShrinkPhysicalFrameLength(static_cast<uint16_t>(rangeStartPhysicalSlot + m_rangeOpInfoDecoder.m_requiredRangeSize) /*expectedCurrentSize*/,
                                                    static_cast<uint16_t>(rangeStartPhysicalSlot + outputUsedLen) /*newSize*/);
                for (size_t i = outputUsedLen; i--;)
                {
                    if (m_isRangeOpSlotHoldingOutput[i] == 0)
                    {
                        m_manager.MarkUnaccountedPhysicalSlotAsFree(static_cast<uint16_t>(rangeStartPhysicalSlot + i));
                    }
                }
            }
            else
            {
                m_manager.ShrinkPhysicalFrameLength(static_cast<uint16_t>(rangeStartPhysicalSlot + m_rangeOpInfoDecoder.m_requiredRangeSize) /*expectedCurrentSize*/,
                                                    static_cast<uint16_t>(rangeStartPhysicalSlot) /*newSize*/);
            }
        }
        AdvanceCurrentUseIndex();
    }

    void ProcessDeathEvent(ValueRegAllocInfo* ssaVal)
    {
        m_manager.AssertValueLiveAndRegInfoOK(ssaVal);
        TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        if (ssaVal->IsAvailableInGPR())
        {
            m_gprAlloc.ProcessDeathEvent(ssaVal->GetGprOrdInList(), ssaVal);
        }
        if (ssaVal->IsAvailableInFPR())
        {
            m_fprAlloc.ProcessDeathEvent(ssaVal->GetFprOrdInList(), ssaVal);
        }
        TestAssert(!ssaVal->IsAvailableInGPR() && !ssaVal->IsAvailableInFPR());
        m_manager.ProcessDeath(ssaVal);
    }

    void ProcessBasicBlock(BasicBlockCodegenInfo* cbb)
    {
        BasicBlock* bb = cbb->m_bb;

        {
            JITCodeSizeInfo jitCodeSizeInfoAtBBStart = CodegenLog().GetJitCodeSizeInfo();
            cbb->m_fastPathStartOffset = jitCodeSizeInfoAtBBStart.m_fastPathLength;
            cbb->m_slowPathStartOffset = jitCodeSizeInfoAtBBStart.m_slowPathLength;
            cbb->m_dataSecStartOffset = jitCodeSizeInfoAtBBStart.m_dataSecLength;
            TestAssertImp(cbb->m_isReachedByBackEdge, cbb->m_fastPathStartOffset % 16 == 0);
        }

        cbb->m_slowPathDataStartOffset = m_slowPathDataEndOffset;

        m_manager.ResetForNewBasicBlock();
        m_gprAlloc.Reset();
        m_fprAlloc.Reset();

        m_bbAlloc.Reset();
        m_valueUseListBuilder.ProcessBasicBlock(bb);

        TestAssertIff(bb->GetNumSuccessors() == 2, m_valueUseListBuilder.m_brDecision != nullptr);
        if (bb->GetNumSuccessors() == 2)
        {
            m_nodeOutputtingBranchDecision = bb->GetTerminator();
            TestAssert(m_nodeOutputtingBranchDecision->GetNumNodeControlFlowSuccessors() == 2);
        }

        TestAssert(m_valueUseListBuilder.m_nodeInfo.size() == bb->m_nodes.size());

        TestAssert(m_valueUseListBuilder.m_nextSpillAllUseIndexList.size() > 0);
        m_nextSpillEverythingUseIndex = &m_valueUseListBuilder.m_nextSpillAllUseIndexList.back();
        m_currentUseIndex = 1;

        NodeRegAllocInfo** nodeInfoList = m_valueUseListBuilder.m_nodeInfo.data();
        ArenaPtr<Node>* nodeList = bb->m_nodes.data();
        size_t numNodes = bb->m_nodes.size();
        m_gprAlloc.AssertConsistency();
        m_fprAlloc.AssertConsistency();
        m_manager.AssertAllSpillSlotsAreAccounted();
        for (size_t nodeIdx = 0; nodeIdx < numNodes; nodeIdx++)
        {
            TestAssert(m_currentUseIndex == nodeIdx * 3 + 1);
            NodeRegAllocInfo* nodeInfo = nodeInfoList[nodeIdx];
            Node* node = nodeList[nodeIdx];

            // Generate code for the node
            //
            if (node->IsBuiltinNodeKind())
            {
                ProcessBuiltinNode(node, nodeInfo);
            }
            else
            {
                ProcessGuestLanguageNode(node, nodeInfo);
            }
            m_manager.AssertAllSpillSlotsAreAccounted();
            m_gprAlloc.AssertConsistency();
            m_fprAlloc.AssertConsistency();

            // Process all SSA input operand death events
            //
            for (ValueUseRAInfo op : nodeInfo->GetAllOperands())
            {
                if (op.IsLastUse())
                {
                    ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                    ProcessDeathEvent(ssaVal);
                }
            }

            // It's also possible that some SSA outputs have no use at all, and they also become dead now
            //
            {
                bool hasDirectOutput = node->HasDirectOutput();
                size_t numExtraOutputs = node->GetNumExtraOutputs();
                if (hasDirectOutput || numExtraOutputs > 0)
                {
                    ValueRegAllocInfo* outputSSAVals = node->GetValueRegAllocInfo();
                    for (size_t i = (hasDirectOutput ? 0 : 1); i <= numExtraOutputs; i++)
                    {
                        ValueRegAllocInfo* ssaVal = outputSSAVals + i;
                        m_manager.AssertValueLiveAndRegInfoOK(ssaVal);
                        if (ssaVal->HasNoMoreUseInBothGprAndFpr())
                        {
                            ProcessDeathEvent(ssaVal);
                        }
                    }
                }
            }

            m_gprAlloc.AssertConsistency();
            m_fprAlloc.AssertConsistency();
            m_manager.AssertAllSpillSlotsAreAccounted();
        }
        TestAssert(m_currentUseIndex == numNodes * 3 + 1);

        if (m_valueUseListBuilder.m_brDecision != nullptr)
        {
            ValueRegAllocInfo* brDecision = m_valueUseListBuilder.m_brDecision;
            TestAssert(!brDecision->HasNoMoreUseInBothGprAndFpr());
            if (brDecision->IsAvailableInGPR())
            {
                cbb->m_terminatorInfo.SetConditionValueAsReg(brDecision->GetGprRegister());
            }
            else
            {
                TestAssert(brDecision->IsSpilled());
                cbb->m_terminatorInfo.SetConditionValueAsStackSlot(brDecision->GetPhysicalSpillSlot());
            }
            // For consistency, update NextUse and process the death event as well
            //
            TestAssert(m_valueUseListBuilder.m_brDecisionUse != nullptr);
            ValueUseRAInfo use = *m_valueUseListBuilder.m_brDecisionUse;
            TestAssert(use.IsGPRUse());
            brDecision->SetGprNextUseInfo(use.GetNextUseInfo());
            if (brDecision->IsAvailableInGPR())
            {
                m_gprAlloc.UpdateNextUseInfo(brDecision->GetGprOrdInList(), use);
            }
            TestAssert(brDecision->HasNoMoreUseInBothGprAndFpr());
            ProcessDeathEvent(brDecision);
        }

        // Assert consistency of all state trackers at the end of the basic block
        //
        m_gprAlloc.AssertConsistency();
        m_gprAlloc.AssertAllRegistersScratched();
        m_fprAlloc.AssertConsistency();
        m_fprAlloc.AssertAllRegistersScratched();
        m_manager.AssertConsistencyAtBasicBlockEnd();
        m_valueUseListBuilder.AssertValueRegAllocInfoForAllConstantLikeNodeInResettedState();

        // Set up the expectations after replaying the codegen log
        //
        {
            JITCodeSizeInfo csi = CodegenLog().GetJitCodeSizeInfo();
            cbb->m_expectedFastPathOffsetAfterLogReplay = csi.m_fastPathLength;
            cbb->m_expectedSlowPathOffsetAfterLogReplay = csi.m_slowPathLength;
            cbb->m_expectedDataSecOffsetAfterLogReplay = csi.m_dataSecLength;
            cbb->m_expectedSlowPathDataOffsetAfterLogReplay = m_slowPathDataEndOffset;
        }

        // At this moment the terminatorInfo should be fully set up
        //
        cbb->m_terminatorInfo.AssertConsistency();

        // Set up various codegen information, and update JIT code size info
        //
        cbb->m_codegenLog = CodegenLog().CloneAndGetCodegenLog(m_passAlloc);
        cbb->m_slowPathDataRegConfStream = CodegenLog().CloneAndGetSlowPathDataRegConfigStream(m_passAlloc);

        // The fast path needs to be added the code length for the terminator
        //
        CodegenLog().AppendOpaqueFastPathJitCode(cbb->m_terminatorInfo.GetJITCodeLength());

        if (cbb->m_shouldPadFastPathTo16ByteAlignmentAtEnd)
        {
            uint32_t oldTotalFastPathLen = CodegenLog().GetJitCodeSizeInfo().m_fastPathLength;
            uint32_t newTotalFastPathLen = RoundUpToMultipleOf<16>(oldTotalFastPathLen);
            TestAssert(newTotalFastPathLen - oldTotalFastPathLen < 16);
            cbb->m_addNopBytesAtEnd = static_cast<uint8_t>(newTotalFastPathLen - oldTotalFastPathLen);
            CodegenLog().AppendOpaqueFastPathJitCode(cbb->m_addNopBytesAtEnd);
            TestAssert(CodegenLog().GetJitCodeSizeInfo().m_fastPathLength % 16 == 0);
        }
        else
        {
            cbb->m_addNopBytesAtEnd = 0;
        }
    }

    // Set up the constant table
    //
    static void SetupConstantTableForDfgCodeBlock(RestrictPtr<uint64_t> dcb, RestrictPtr<uint64_t> cstTableData, size_t numElements)
    {
        uint64_t* end = cstTableData + numElements;
        while (cstTableData < end)
        {
            dcb--;
            *dcb = *cstTableData;
            cstTableData++;
        }
    }

    struct CodegenControlState
    {
        PrimaryCodegenState m_pcs;
        // Helper fields for assertion purposes
        //
        uint8_t* m_fastPathBaseAddr;
        uint8_t* m_slowPathBaseAddr;
        uint8_t* m_dataSecBaseAddr;
        DfgCodeBlock* m_dcb;
    };

    void CodegenBasicBlock(BasicBlockCodegenInfo* cbb, [[maybe_unused]] size_t cbbIdx, CodegenControlState& ccs /*inout*/)
    {
        TestAssert(ccs.m_pcs.m_fastPathAddr == ccs.m_fastPathBaseAddr + cbb->m_fastPathStartOffset);
        TestAssert(ccs.m_pcs.m_slowPathAddr == ccs.m_slowPathBaseAddr + cbb->m_slowPathStartOffset);
        TestAssert(ccs.m_pcs.m_dataSecAddr == ccs.m_dataSecBaseAddr + cbb->m_dataSecStartOffset);
        TestAssert(ccs.m_pcs.m_slowPathDataAddr == reinterpret_cast<uint8_t*>(ccs.m_dcb) + cbb->m_slowPathDataStartOffset);
        TestAssert(ccs.m_pcs.m_slowPathDataOffset == cbb->m_slowPathDataStartOffset);

        TestAssertImp(cbb->m_isReachedByBackEdge, reinterpret_cast<uint64_t>(ccs.m_pcs.m_fastPathAddr) % 16 == 0);

#ifdef TESTBUILD
        fprintf(m_codegenLogDumpContext.m_file, "BasicBlock #%llu:\n", static_cast<unsigned long long>(cbbIdx));
#endif

        ccs.m_pcs.m_compactedRegConfAddr = cbb->m_slowPathDataRegConfStream.data();

        [[maybe_unused]] uint8_t* expectedSlowPathRegConfigStreamEnd = cbb->m_slowPathDataRegConfStream.data() + cbb->m_slowPathDataRegConfStream.size();
        [[maybe_unused]] uint8_t* expectedFastPathPtrAfterLogReplay = ccs.m_fastPathBaseAddr + cbb->m_expectedFastPathOffsetAfterLogReplay;
        [[maybe_unused]] uint8_t* expectedSlowPathPtrAfterLogReplay = ccs.m_slowPathBaseAddr + cbb->m_expectedSlowPathOffsetAfterLogReplay;
        [[maybe_unused]] uint8_t* expectedDataSecPtrAfterLogReplay = ccs.m_dataSecBaseAddr + cbb->m_expectedDataSecOffsetAfterLogReplay;
        [[maybe_unused]] uint8_t* expectedSlowPathDataPtrAfterLogReplay = reinterpret_cast<uint8_t*>(ccs.m_dcb) + cbb->m_expectedSlowPathDataOffsetAfterLogReplay;

        void* codegenLogCurPtr = cbb->m_codegenLog.data();
        uint8_t* codegenLogEnd = cbb->m_codegenLog.data() + cbb->m_codegenLog.size();

        while (codegenLogCurPtr < codegenLogEnd)
        {
            CodegenOpBase* cgAction = std::launder(reinterpret_cast<CodegenOpBase*>(codegenLogCurPtr));
            void* nextCgOp = cgAction->Dispatch(
                [&]<typename CodegenOp>(CodegenOp* op) ALWAYS_INLINE -> void* /*nextCodegenOpAddr*/
                {
                    static_assert(std::is_base_of_v<CodegenOpBase, CodegenOp>);
#ifdef TESTBUILD
                    // Generate the human-readable codegen log in test build
                    //
                    fprintf(m_codegenLogDumpContext.m_file, "   0x%llx:",
                            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(ccs.m_pcs.m_fastPathAddr)));
                    op->DumpHumanReadableLog(m_codegenLogDumpContext);
#endif
                    // Generate JIT code and advance to the next codegen op
                    //
                    op->DoCodegen(ccs.m_pcs /*inout*/);
                    return op->GetStructEnd();
                });

            // Assert that no pointers have overflowed so we can catch these bugs ASAP before more stuffs gets corrupted
            //
            TestAssert(ccs.m_pcs.m_fastPathAddr <= expectedFastPathPtrAfterLogReplay);
            TestAssert(ccs.m_pcs.m_slowPathAddr <= expectedSlowPathPtrAfterLogReplay);
            TestAssert(ccs.m_pcs.m_dataSecAddr <= expectedDataSecPtrAfterLogReplay);
            TestAssert(ccs.m_pcs.m_slowPathDataAddr <= expectedSlowPathDataPtrAfterLogReplay);
            TestAssert(ccs.m_pcs.m_compactedRegConfAddr <= expectedSlowPathRegConfigStreamEnd);
            TestAssert(ccs.m_pcs.m_slowPathDataAddr == reinterpret_cast<uint8_t*>(ccs.m_dcb) + ccs.m_pcs.m_slowPathDataOffset);
            TestAssert(ccs.m_pcs.m_dfgCodeBlockLower32Bits == static_cast<uint32_t>(reinterpret_cast<uint64_t>(ccs.m_dcb)));

            TestAssert(nextCgOp > codegenLogCurPtr);
            TestAssert(nextCgOp <= codegenLogEnd);
            codegenLogCurPtr = nextCgOp;
        }

        // Assert various pointers are as expected
        //
        TestAssert(codegenLogCurPtr == codegenLogEnd);
        TestAssert(ccs.m_pcs.m_fastPathAddr == expectedFastPathPtrAfterLogReplay);
        TestAssert(ccs.m_pcs.m_slowPathAddr == expectedSlowPathPtrAfterLogReplay);
        TestAssert(ccs.m_pcs.m_dataSecAddr == expectedDataSecPtrAfterLogReplay);
        TestAssert(ccs.m_pcs.m_slowPathDataAddr == expectedSlowPathDataPtrAfterLogReplay);
        TestAssert(ccs.m_pcs.m_compactedRegConfAddr == expectedSlowPathRegConfigStreamEnd);
        TestAssert(ccs.m_pcs.m_slowPathDataAddr == reinterpret_cast<uint8_t*>(ccs.m_dcb) + ccs.m_pcs.m_slowPathDataOffset);
        TestAssert(ccs.m_pcs.m_dfgCodeBlockLower32Bits == static_cast<uint32_t>(reinterpret_cast<uint64_t>(ccs.m_dcb)));

        // Generate the logic at the end of basic block
        //
        cbb->m_terminatorInfo.AssertConsistency();
        TestAssertImp(cbb->m_terminatorInfo.m_isDefaultTargetFallthrough, m_bbOrder.data() + cbb->m_terminatorInfo.m_defaultTargetOrd == cbb + 1);
        cbb->m_terminatorInfo.EmitJITCode(ccs.m_pcs.m_fastPathAddr /*inout*/,
                                          ccs.m_fastPathBaseAddr,
                                          m_bbOrder);

#ifdef TESTBUILD
        cbb->m_terminatorInfo.DumpHumanReadableLog(ccs.m_pcs, m_codegenLogDumpContext);
#endif

        if (cbb->m_addNopBytesAtEnd > 0)
        {
            TestAssert(cbb->m_addNopBytesAtEnd <= 15);
            // The longest x64 NOP is 15 bytes, which happens to also be our maximum padding since we are aligning to 16 bytes,
            // so it can always be represented by one NOP
            //
            const uint8_t* nopInst = GetX64MultiByteNOPInst(cbb->m_addNopBytesAtEnd);
            memcpy(ccs.m_pcs.m_fastPathAddr, nopInst, cbb->m_addNopBytesAtEnd);
            ccs.m_pcs.m_fastPathAddr += cbb->m_addNopBytesAtEnd;
        }

        TestAssertImp(cbb->m_shouldPadFastPathTo16ByteAlignmentAtEnd, reinterpret_cast<uint64_t>(ccs.m_pcs.m_fastPathAddr) % 16 == 0);

#ifdef TESTBUILD
        fprintf(m_codegenLogDumpContext.m_file, "\n");
#endif
    }

    // Generate everything
    //
    void DoCodegen()
    {
        VM* vm = VM::GetActiveVMForCurrentThread();

        // Record final JIT code size info
        //
        {
            JITCodeSizeInfo jitCodeSizeInfo = CodegenLog().GetJitCodeSizeInfo();
            m_totalJitFastPathSectionLen = jitCodeSizeInfo.m_fastPathLength;
            m_totalJitSlowPathSectionLen = jitCodeSizeInfo.m_slowPathLength;
            m_totalJitDataSectionLen = jitCodeSizeInfo.m_dataSecLength;
        }

        // Now we have the full codegen plan and know how many total stack slots we have
        // Patch the constants where we are supposed to add #TotalSlots
        //
        uint32_t totalNumStackSlots = m_manager.GetMaxTotalNumberOfPhysicalSlots();
        for (uint64_t* addr : m_literalFieldToBeAddedByTotalFrameSlots)
        {
            *addr += totalNumStackSlots;
        }

        // Compute the size of DfgCodeBlock and allocate it
        //
        DfgCodeBlock* dcb;
        {
            // Oops, AllocFromSystemHeap always returns 8-byte-aligned memory..
            //
            static_assert(sizeof(TValue) == 8 && alignof(DfgCodeBlock) == 8);
            size_t constantTableLength = sizeof(TValue) * m_stackLayoutPlanningResult.m_constantTable.size();
            TestAssert(m_slowPathDataEndOffset >= sizeof(DfgCodeBlock));
            size_t dfgCodeBlockAllocSize = constantTableLength + m_slowPathDataEndOffset;
            dfgCodeBlockAllocSize = RoundUpToMultipleOf<8>(dfgCodeBlockAllocSize);
            uint8_t* addressBegin = TranslateToRawPointer(vm, vm->AllocFromSystemHeap(static_cast<uint32_t>(dfgCodeBlockAllocSize)).AsNoAssert<uint8_t>());
            dcb = reinterpret_cast<DfgCodeBlock*>(addressBegin + constantTableLength);
            TestAssert(reinterpret_cast<uint64_t>(dcb) % alignof(DfgCodeBlock) == 0);
        }

        ConstructInPlace(dcb);

        SetupConstantTableForDfgCodeBlock(reinterpret_cast<uint64_t*>(dcb),
                                          m_stackLayoutPlanningResult.m_constantTable.data(),
                                          m_stackLayoutPlanningResult.m_constantTable.size());

        CodeBlock* cb = GetGraph()->GetRootCodeBlock();
        dcb->m_globalObject = cb->m_globalObject;
        dcb->m_stackFrameNumSlots = totalNumStackSlots;
        dcb->m_stackRegSpillRegionPhysicalSlot = m_firstRegSpillSlot;
        dcb->m_owner = cb;
        TestAssert(m_slowPathDataEndOffset >= m_slowPathDataStartOffset);
        dcb->m_slowPathDataStreamLength = m_slowPathDataEndOffset - m_slowPathDataStartOffset;

        // Allocate the JIT region
        //     [ data section ] [ JIT fast path ] [ JIT slow path ]
        // Note that however, the codegen may overwrite at most 7 more bytes after each section, so allocation must account for that.
        //
        constexpr size_t x_maxBytesCodegenFnMayOverwrite = 7;

        size_t fastPathBaseOffset = m_totalJitDataSectionLen;
        if (fastPathBaseOffset > 0)
        {
            // Only add the padding if the data section is not empty (it is often empty),
            // since if the data section is empty, the codegen won't write anything at all so the padding is not needed.
            // This way we don't waste 16 bytes if the data section is empty.
            //
            fastPathBaseOffset += x_maxBytesCodegenFnMayOverwrite;
        }

        // Make the function entry address 16-byte aligned, which is also required to fulfill our loop header alignment
        //
        fastPathBaseOffset = RoundUpToMultipleOf<16>(fastPathBaseOffset);

        size_t fastPathSectionEnd = fastPathBaseOffset + m_totalJitFastPathSectionLen;

        size_t slowPathBaseOffset = fastPathSectionEnd + x_maxBytesCodegenFnMayOverwrite;
        size_t slowPathSectionEnd = slowPathBaseOffset + m_totalJitSlowPathSectionLen;

        size_t totalJitRegionSize = slowPathSectionEnd + x_maxBytesCodegenFnMayOverwrite;

        // Allocate the JIT memory
        //
        JitMemoryAllocator* jitAlloc = vm->GetJITMemoryAlloc();
        dcb->m_jitRegionStart = jitAlloc->AllocateGivenSize(totalJitRegionSize);
        dcb->m_jitRegionSize = SafeIntegerCast<uint32_t>(totalJitRegionSize);

        TestAssert(reinterpret_cast<uint64_t>(dcb->m_jitRegionStart) % x_jitMaxPossibleDataSectionAlignment == 0);
        TestAssert(reinterpret_cast<uint64_t>(dcb->m_jitRegionStart) % 16 == 0);

        uint8_t* jitRegionStart = reinterpret_cast<uint8_t*>(dcb->m_jitRegionStart);
        uint8_t* dataSecBasePtr = jitRegionStart;
        uint8_t* fastPathBasePtr = jitRegionStart + fastPathBaseOffset;
        uint8_t* slowPathBasePtr = jitRegionStart + slowPathBaseOffset;

        dcb->m_jitCodeEntry = fastPathBasePtr;
        TestAssert(reinterpret_cast<uint64_t>(dcb->m_jitCodeEntry) % 16 == 0);

        CodegenControlState ccs;
        ccs.m_pcs.m_fastPathAddr = fastPathBasePtr;
        ccs.m_pcs.m_slowPathAddr = slowPathBasePtr;
        ccs.m_pcs.m_dataSecAddr = dataSecBasePtr;
        TestAssert(m_slowPathDataStartOffset > 0);
        ccs.m_pcs.m_slowPathDataAddr = reinterpret_cast<uint8_t*>(dcb) + m_slowPathDataStartOffset;
        ccs.m_pcs.m_slowPathDataOffset = m_slowPathDataStartOffset;
        // This field is set up by each BB
        //
        ccs.m_pcs.m_compactedRegConfAddr = nullptr;
        ccs.m_pcs.m_dfgCodeBlockLower32Bits = static_cast<uint32_t>(reinterpret_cast<uint64_t>(dcb));
        ccs.m_fastPathBaseAddr = fastPathBasePtr;
        ccs.m_slowPathBaseAddr = slowPathBasePtr;
        ccs.m_dataSecBaseAddr = dataSecBasePtr;
        ccs.m_dcb = dcb;

        // Generate the function entry logic
        //
        {
            TestAssert(m_functionEntryTrait.m_emitterFn != nullptr);
            m_functionEntryTrait.m_emitterFn(ccs.m_pcs.m_fastPathAddr, ccs.m_pcs.m_slowPathAddr, ccs.m_pcs.m_dataSecAddr);
            ccs.m_pcs.m_fastPathAddr += m_functionEntryTrait.m_fastPathCodeLen;
            ccs.m_pcs.m_slowPathAddr += m_functionEntryTrait.m_slowPathCodeLen;
            ccs.m_pcs.m_dataSecAddr += m_functionEntryTrait.m_dataSecCodeLen;
        }

#ifdef TESTBUILD
        m_codegenLogDumpPtr = nullptr;
        m_codegenLogDumpSize = 0;
        m_codegenLogDumpContext.m_file = open_memstream(&m_codegenLogDumpPtr, &m_codegenLogDumpSize);
        TestAssert(m_codegenLogDumpContext.m_file != nullptr);
        m_codegenLogDumpContext.m_firstRegSpillPhysicalSlot = m_firstRegSpillSlot;
        m_codegenLogDumpContext.m_constantTable = m_stackLayoutPlanningResult.m_constantTable.data();
        m_codegenLogDumpContext.m_constantTableSize = m_stackLayoutPlanningResult.m_constantTable.size();
#endif

        TestAssert(m_bbOrder.size() > 0);
        for (size_t cbbIdx = 0; cbbIdx < m_bbOrder.size(); cbbIdx++)
        {
            BasicBlockCodegenInfo* cbb = m_bbOrder.data() + cbbIdx;
            CodegenBasicBlock(cbb, cbbIdx, ccs /*inout*/);
        }

        TestAssert(ccs.m_pcs.m_fastPathAddr == jitRegionStart + fastPathSectionEnd);
        TestAssert(ccs.m_pcs.m_slowPathAddr == jitRegionStart + slowPathSectionEnd);
        TestAssert(ccs.m_pcs.m_dataSecAddr == jitRegionStart + m_totalJitDataSectionLen);
        TestAssert(ccs.m_pcs.m_slowPathDataAddr == reinterpret_cast<uint8_t*>(dcb) + m_slowPathDataEndOffset);
        TestAssert(ccs.m_pcs.m_slowPathDataOffset == m_slowPathDataEndOffset);

        // Populate the trailing space after the fast path and slow path JIT code
        //
        TestAssert(ccs.m_pcs.m_fastPathAddr + x_maxBytesCodegenFnMayOverwrite == slowPathBasePtr);
        TestAssert(ccs.m_pcs.m_slowPathAddr + x_maxBytesCodegenFnMayOverwrite == jitRegionStart + totalJitRegionSize);

        auto populateCodeGap = [&](uint8_t* addr) ALWAYS_INLINE
        {
            [[maybe_unused]] uint8_t* oldAddr = addr;

            TestAssert(x_maxBytesCodegenFnMayOverwrite >= 2);
            EmitUd2Instruction(addr /*inout*/);
            TestAssert(addr == oldAddr + 2);

            size_t numNopBytes = x_maxBytesCodegenFnMayOverwrite - 2;
            TestAssert(numNopBytes <= 15);
            const uint8_t* nopInst = GetX64MultiByteNOPInst(numNopBytes);
            memcpy(addr, nopInst, numNopBytes);
            TestAssert(addr + numNopBytes == oldAddr + x_maxBytesCodegenFnMayOverwrite);
        };
        populateCodeGap(ccs.m_pcs.m_fastPathAddr);
        populateCodeGap(ccs.m_pcs.m_slowPathAddr);

        m_resultDcb = dcb;

#ifdef TESTBUILD
        // Finalize the human-readable log dump
        // open_memstream use malloc to allocate the memory.
        // We expect the result to be managed by a TempAllocator in consistent with the other places
        // to avoid memory leak (even though this happens only in test build), so make a copy using our alloc
        //
        {
            // Must close file to flush everything!
            //
            fclose(m_codegenLogDumpContext.m_file);
            m_codegenLogDumpContext.m_file = nullptr;

            // Need a terminating NULL
            //
            char* resultBuf = m_resultAlloc.AllocateArray<char>(m_codegenLogDumpSize + 1);
            memcpy(resultBuf, m_codegenLogDumpPtr, m_codegenLogDumpSize);
            resultBuf[m_codegenLogDumpSize] = '\0';
            // Buffer managed by open_memstream should be freed by 'free'
            //
            free(m_codegenLogDumpPtr);
            m_codegenLogDumpPtr = resultBuf;
        }
#endif
    }

    // Return the index of the block in m_bbOrder
    //
    uint32_t DfsBasicBlock(BasicBlock* bb)
    {
        if (bb->m_ordInCodegenOrder != static_cast<uint32_t>(-1))
        {
            TestAssert(bb->m_ordInCodegenOrder < m_bbOrder.size());
            TestAssert(m_bbOrder[bb->m_ordInCodegenOrder].m_bb == bb);
            if (m_bbOrder[bb->m_ordInCodegenOrder].m_isOnDfsStack)
            {
                m_bbOrder[bb->m_ordInCodegenOrder].m_isReachedByBackEdge = true;
            }
            return bb->m_ordInCodegenOrder;
        }

        TestAssert(m_bbOrder.size() < GetGraph()->m_blocks.size());
        TestAssert(m_bbOrder.size() < m_bbOrder.capacity());
        bb->m_ordInCodegenOrder = static_cast<uint32_t>(m_bbOrder.size());

        // This reference will remain valid because we have reserved enough capacity
        //
        BasicBlockCodegenInfo& info = m_bbOrder.emplace_back();
        info.m_isOnDfsStack = true;
        info.m_bb = bb;
        if (bb->GetNumSuccessors() == 0)
        {
            info.m_terminatorInfo.InitNoSuccessor();
        }
        else if (bb->GetNumSuccessors() == 1)
        {
            uint32_t destOrd = DfsBasicBlock(bb->GetSuccessor(0));
            info.m_terminatorInfo.InitUnconditionalBranch(bb->m_ordInCodegenOrder, destOrd);
        }
        else
        {
            TestAssert(bb->GetNumSuccessors() == 2);
            uint32_t defaultDestOrd = DfsBasicBlock(bb->GetSuccessor(0));
            uint32_t branchDestOrd = DfsBasicBlock(bb->GetSuccessor(1));
            info.m_terminatorInfo.InitCondBranch(bb->m_ordInCodegenOrder, defaultDestOrd, branchDestOrd);
        }
        info.m_isOnDfsStack = false;
        return bb->m_ordInCodegenOrder;
    }

    // Figure out the order of the basic blocks in the JIT code
    //
    void PlanBasicBlockOrder()
    {
        for (BasicBlock* bb : GetGraph()->m_blocks)
        {
            bb->m_ordInCodegenOrder = static_cast<uint32_t>(-1);
        }

        // Must reserve enough capacity to avoid iterator invalidation!
        //
        m_bbOrder.clear();
        m_bbOrder.reserve(GetGraph()->m_blocks.size());

        TestAssert(GetGraph()->m_blocks.size() > 0);
        DfsBasicBlock(GetGraph()->m_blocks[0]);

        TestAssert(m_bbOrder.size() > 0);
        TestAssert(m_bbOrder.size() <= GetGraph()->m_blocks.size());

#ifdef TESTBUILD
        for (size_t idx = 0; idx < m_bbOrder.size(); idx++)
        {
            TestAssert(m_bbOrder[idx].m_bb->m_ordInCodegenOrder == idx);
            TestAssert(!m_bbOrder[idx].m_isOnDfsStack);
        }
        TestAssert(!m_bbOrder[0].m_isReachedByBackEdge);
#endif

        for (size_t idx = 0; idx + 1 < m_bbOrder.size(); idx++)
        {
            if (m_bbOrder[idx + 1].m_isReachedByBackEdge)
            {
                m_bbOrder[idx].m_shouldPadFastPathTo16ByteAlignmentAtEnd = true;
            }
        }
    }

    CodegenOperationLog& WARN_UNUSED CodegenLog()
    {
        return m_manager.GetCodegenLog();
    }

    Graph* GetGraph() { return m_valueUseListBuilder.m_graph; }

    void SetupFunctionEntryLogic()
    {
        CodeBlock* rootCodeBlock = GetGraph()->GetRootCodeBlock();
        TestAssert(m_functionEntryTrait.m_emitterFn == nullptr);
        m_functionEntryTrait = GetDfgJitFunctionEntryLogicTrait(rootCodeBlock->m_hasVariadicArguments, rootCodeBlock->m_numFixedArguments);
        TestAssert(m_functionEntryTrait.m_emitterFn != nullptr);
        TestAssert(CodegenLog().GetJitCodeSizeInfo().m_fastPathLength == 0 &&
                   CodegenLog().GetJitCodeSizeInfo().m_slowPathLength == 0 &&
                   CodegenLog().GetJitCodeSizeInfo().m_dataSecLength == 0);
        JITCodeSizeInfo fnEntryInfo;
        fnEntryInfo.m_fastPathLength = m_functionEntryTrait.m_fastPathCodeLen;
        fnEntryInfo.m_slowPathLength = m_functionEntryTrait.m_slowPathCodeLen;
        fnEntryInfo.m_dataSecLength = m_functionEntryTrait.m_dataSecCodeLen;
        CodegenLog().SetJITCodeSizeInfo(fnEntryInfo);
    }

    void RunImpl()
    {
        m_slowPathDataStartOffset = static_cast<uint32_t>(DfgCodeBlock::GetSlowPathDataStartOffset());
        m_slowPathDataEndOffset = m_slowPathDataStartOffset;
        SetupFunctionEntryLogic();
        PlanBasicBlockOrder();
        InitializeUseListBuilder();
        for (BasicBlockCodegenInfo& cbb : m_bbOrder)
        {
            ProcessBasicBlock(&cbb);
        }
        DoCodegen();
    }

    [[maybe_unused]] TempArenaAllocator& m_resultAlloc;
    TempArenaAllocator m_passAlloc;
    TempArenaAllocator m_bbAlloc;
    StackLayoutPlanningResult& m_stackLayoutPlanningResult;
    ValueUseListBuilder m_valueUseListBuilder;
    RegAllocValueManager m_manager;
    RegAllocDecisionMaker<true /*forGprState*/, RegAllocValueManager> m_gprAlloc;
    RegAllocDecisionMaker<false /*forGprState*/, RegAllocValueManager> m_fprAlloc;
    Node* m_nodeOutputtingBranchDecision;
    uint32_t* m_nextSpillEverythingUseIndex;
    uint32_t m_currentUseIndex;
    uint16_t m_firstRegSpillSlot;
    uint32_t m_totalJitFastPathSectionLen;
    uint32_t m_totalJitSlowPathSectionLen;
    uint32_t m_totalJitDataSectionLen;
    // This is the start/end byte offset of SlowPathData from the address of the DfgCodeBlock
    // (since the JIT code always expects a direct offset from DfgCodeBlock)
    //
    uint32_t m_slowPathDataStartOffset;
    uint32_t m_slowPathDataEndOffset;
    JitFunctionEntryLogicTraits m_functionEntryTrait;
    NodeRangeOperandInfoDecoder m_rangeOpInfoDecoder;
    TempVector<uint8_t> m_isRangeOpSlotHoldingOutput;
    TempVector<uint32_t> m_createFnObjUvIndexList;
    TempVector<uint64_t*> m_literalFieldToBeAddedByTotalFrameSlots;
    TempVector<BasicBlockCodegenInfo> m_bbOrder;
    DfgCodeBlock* m_resultDcb;
#ifdef TESTBUILD
    CodegenLogDumpContext m_codegenLogDumpContext;
    char* m_codegenLogDumpPtr;
    size_t m_codegenLogDumpSize;
#endif
};

}   // anonymous namespace

DfgBackendResult WARN_UNUSED RunDfgBackend(TempArenaAllocator& resultAlloc, Graph* graph, StackLayoutPlanningResult& stackLayoutPlanningResult)
{
    return DfgBackend::Run(resultAlloc, graph, stackLayoutPlanningResult);
}

}   // namespace dfg
