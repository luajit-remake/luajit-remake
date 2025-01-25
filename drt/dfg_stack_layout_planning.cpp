#include "dfg_stack_layout_planning.h"
#include "dfg_osr_call_frame_basemap.h"
#include "dfg_node.h"

namespace dfg {

namespace {

struct InlinedFramePhysicalSlotInfo
{
    // Note that m_parentFrame is always initialized to nullptr in this function
    //
    template<typename Func>
    ALWAYS_INLINE InlinedFramePhysicalSlotInfo(TempArenaAllocator& tempAlloc,
                                               InlinedCallFrame* frame,
                                               const Func& constantHandlerCallback)
        : m_freePhysicalSlots(tempAlloc)
    {
        if (!frame->IsRootFrame())
        {
            m_stackBaseIdx = SafeIntegerCast<uint32_t>(frame->MaxVarArgsAllowed() + x_numSlotsForStackFrameHeader);
            m_absoluteInterpreterSlotStart = SafeIntegerCast<uint32_t>(frame->GetInterpreterSlotForFrameStart().Value());
        }
        else
        {
            m_stackBaseIdx = 0;
            m_absoluteInterpreterSlotStart = 0;
        }
        m_frameLength = m_stackBaseIdx + frame->GetNumBytecodeLocals();
        m_info = tempAlloc.AllocateArray<uint32_t>(m_frameLength);
        memset(m_info, 0, sizeof(uint32_t) * m_frameLength);
        m_inlinedCallFrameOrd = frame->GetInlineCallFrameOrdinal();
        m_totalPhysicalSlots = static_cast<uint32_t>(-1);
        m_parentFrame = nullptr;
        m_frameInfo = frame;

        if (!frame->IsRootFrame())
        {
            TestAssert(m_stackBaseIdx >= x_numSlotsForStackFrameHeader);
            size_t idx = m_stackBaseIdx - x_numSlotsForStackFrameHeader;
            for (uint32_t ord = 0; ord < x_numSlotsForStackFrameHeader; ord++)
            {
                InterpreterSlot islot = frame->GetInterpreterSlotForStackFrameHeader(ord);
                VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(islot);
                TestAssert(vrmi.IsLive());
                if (vrmi.IsUmmapedToAnyVirtualReg())
                {
                    Node* node = vrmi.GetConstantValue();
                    constantHandlerCallback(node);
                    ArenaPtr<Node> nodePtr = vrmi.GetConstantValue();
                    TestAssert(nodePtr.m_value < (1U << 31));
                    m_info[idx + ord] = nodePtr.m_value | (1U << 31);
                }
            }

#ifdef TESTBUILD
            // Only the stack frame header may have IsUmmapedToAnyVirtualReg() slots, the varargs part shouldn't.
            // Assert this. (Note that by design all locals are mapped directly to VirtualRegister, so no need to check locals)
            //
            for (uint32_t ord = 0; ord < m_stackBaseIdx - x_numSlotsForStackFrameHeader; ord++)
            {
                InterpreterSlot islot = frame->GetInterpreterSlotForVariadicArgument(ord);
                VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(islot);
                TestAssert(vrmi.IsLive());
                TestAssert(!vrmi.IsUmmapedToAnyVirtualReg());
            }
#endif
        }
    }

    uint32_t* GetInfoAddrForLocation(InterpreterFrameLocation loc)
    {
        size_t interpreterSlot = m_frameInfo->GetInterpreterSlotForLocation(loc).Value();
        TestAssert(m_absoluteInterpreterSlotStart <= interpreterSlot && interpreterSlot < m_absoluteInterpreterSlotStart + m_frameLength);
        return m_info + (interpreterSlot - m_absoluteInterpreterSlotStart);
    }

    bool IsSlotLiveAndMapped(size_t idx)
    {
        TestAssert(idx < m_frameLength);
        TestAssert(m_info[idx] != 1);
        return static_cast<int32_t>(m_info[idx]) > 0;
    }

    // The physical slot info for each slot in the call frame:
    //     [ varargs ] [ hdr ] [ locals ]
    //
    // m_stackBaseIdx is the index for local 0, and m_frameLength is the total number of slots above
    //
    // A value in m_info should be interpreted as follows:
    //     if bit 31 == 0, then if the value:
    //         =0: it has no DFG physical slot since no SetLocal ever writes to it
    //         =1: it needs to be allocated a DFG physical slot, but haven't done so yet
    //         >1: the value is the DFG physical slot ordinal (note that the reg spill area precedes the
    //             the first local slot, so the ordinal is always >=#regs, which is >1)
    //     if bit 31 == 1:
    //         The slot holds a statically-known constant, and the lower 31 bits should be interpreted
    //         as an ArenaPtr<Node>, which is the value of the constant
    //
    uint32_t* m_info;
    uint32_t m_stackBaseIdx;
    uint32_t m_frameLength;
    // The free list of currently unused DFG physical slots
    //
    TempVector<uint32_t> m_freePhysicalSlots;
    uint32_t m_inlinedCallFrameOrd;
    uint32_t m_totalPhysicalSlots;
    uint32_t m_absoluteInterpreterSlotStart;
    InlinedFramePhysicalSlotInfo* m_parentFrame;
    InlinedCallFrame* m_frameInfo;
};

// We require that GetLocal can only see SetLocal operating on the same InlinedCallFrame,
// so for an inlined call, we will always use GetLocal to load the argument in the parent frame,
// then use SetLocals to set up the argument in the callee frame.
// But now we are generating JIT code, if the parent frame argument is dead at callee entry (since the call
// is in-place or tail call), we can assign the same physical slot for the GetLocal and SetLocal,
// so that the SetLocal physically a no-op.
//
// The DFG frontend already attempts to assign the same VirtualReg ordinal for arguments in the callee as
// their VirtualReg in the caller (so the above-mentioned GetLocal/SetLocal pair will operate on the
// same VirtualRegister ordinal if possible). So all we need to do is to attempt to assign the same physical
// slot for the same VirtualRegister ordinal.
//
// A few notes:
// 1. The SetLocals still need to be there, since it affects OSR exit records. It's only that we can generate
//    no JIT code for it.
// 2. In theory we can also make the GetLocal no-op, but in that case we will also need to teach
//    the ShadowStore that the value is already on the stack, so we leave doing this to the future.
//
struct VRegAwarePhysicalSlotFreeList
{
    VRegAwarePhysicalSlotFreeList(TempArenaAllocator& tempAlloc, uint32_t numVRegs)
        : m_length(numVRegs)
        , m_availbleVRegs(tempAlloc)
    {
        m_phySlotForVReg = tempAlloc.AllocateArray<uint32_t>(m_length);
        memset(m_phySlotForVReg, 0, sizeof(uint32_t) * numVRegs);
    }

    [[maybe_unused]] bool WARN_UNUSED IsInCleanState()
    {
        return m_availbleVRegs.empty();
    }

    bool WARN_UNUSED VRegAvailable(uint32_t vregOrd)
    {
        TestAssert(vregOrd < m_length);
        return m_phySlotForVReg[vregOrd] != 0;
    }

    void AddFree(uint32_t vregOrd, uint32_t phySlotOrd)
    {
        TestAssert(phySlotOrd >= 2);
        TestAssert(!VRegAvailable(vregOrd));
        m_phySlotForVReg[vregOrd] = phySlotOrd;
        m_availbleVRegs.push_back(vregOrd);
    }

    uint32_t WARN_UNUSED ConsumePhySlotForVReg(uint32_t vregOrd)
    {
        TestAssert(VRegAvailable(vregOrd));
        uint32_t phySlot = m_phySlotForVReg[vregOrd];
        m_phySlotForVReg[vregOrd] = 0;
        return phySlot;
    }

    void AddAllRemainingPhySlotsToFreeList(TempVector<uint32_t>& freeList /*out*/)
    {
        for (uint32_t vregOrd : m_availbleVRegs)
        {
            if (VRegAvailable(vregOrd))
            {
                freeList.push_back(m_phySlotForVReg[vregOrd]);
                m_phySlotForVReg[vregOrd] = 0;
            }
        }
        m_availbleVRegs.clear();
    }

private:
    size_t m_length;
    uint32_t* m_phySlotForVReg;
    TempVector<uint32_t /*vregOrd*/> m_availbleVRegs;
};

struct StackLayoutPlanningPass
{
    static StackLayoutPlanningResult WARN_UNUSED RunPass(TempArenaAllocator& resultAlloc, Graph* graph)
    {
        TestAssert(graph->IsBlockLocalSSAForm());

        StackLayoutPlanningPass pass(resultAlloc, graph);
        pass.RunPassImpl();

        StackLayoutPlanningResult r(resultAlloc);
        r.m_constantTable = std::move(pass.m_finalConstantTable);
        r.m_inlineFrameOsrInfoOffsets = pass.m_inlineFrameOsrInfoOffsets;
        r.m_inlineFrameOsrInfoDataBlock = pass.m_inlineFrameOsrInfoData;
        r.m_m_inlineFrameOsrInfoDataBlockSize = pass.m_inlineFrameOsrInfoDataSize;
        r.m_numTotalPhysicalSlots = pass.m_numTotalPhysicalSlots;
        r.m_numTotalBoxedConstants = pass.m_numBoxedConstants;

        TestAssert(r.m_inlineFrameOsrInfoOffsets != nullptr);
        TestAssert(r.m_inlineFrameOsrInfoDataBlock != nullptr);
        TestAssert(r.m_m_inlineFrameOsrInfoDataBlockSize != static_cast<uint32_t>(-1));
        TestAssert(r.m_numTotalPhysicalSlots != static_cast<uint32_t>(-1));
        TestAssert(r.m_numTotalBoxedConstants != static_cast<uint32_t>(-1));

        return r;
    }

private:
    ALWAYS_INLINE StackLayoutPlanningPass(TempArenaAllocator& resultAlloc, Graph* graph)
        : m_resultAlloc(resultAlloc)
        , m_tempAlloc()
        , m_finalConstantTable(m_resultAlloc)
        , m_constantTableForUnboxedConstants(m_tempAlloc)
        , m_unboxedConstantsToFix(m_tempAlloc)
        , m_numBoxedConstants(static_cast<uint32_t>(-1))
        , m_numTotalPhysicalSlots(static_cast<uint32_t>(-1))
        , m_inlineFrameOsrInfoOffsets(nullptr)
        , m_inlineFrameOsrInfoData(nullptr)
        , m_inlineFrameOsrInfoDataSize(static_cast<uint32_t>(-1))
        , m_graph(graph)
    { }

    void ALWAYS_INLINE ReserveSlotInConstantTable(Node* node)
    {
        TestAssert(node->IsConstantNode() || node->IsUnboxedConstantNode());
        if (node->IsOrdInConstantTableAssigned())
        {
            return;
        }

        if (node->IsConstantNode())
        {
            uint64_t value = node->GetConstantNodeValue().m_value;
            int64_t ord = -static_cast<int64_t>(m_finalConstantTable.size()) - 1;
            m_finalConstantTable.push_back(value);
            node->AssignConstantNodeOrdInConstantTable(ord);
        }
        else
        {
            uint64_t value = node->GetUnboxedConstantNodeValue();
            int64_t ord = -static_cast<int64_t>(m_constantTableForUnboxedConstants.size()) - 1;
            m_constantTableForUnboxedConstants.push_back(value);
            node->AssignConstantNodeOrdInConstantTable(ord);
            m_unboxedConstantsToFix.push_back(node);
        }
    }

    void ALWAYS_INLINE FinalizeConstantTable()
    {
        TestAssert(m_numBoxedConstants == static_cast<uint32_t>(-1));
        m_numBoxedConstants = SafeIntegerCast<uint32_t>(m_finalConstantTable.size());
        m_finalConstantTable.insert(m_finalConstantTable.end(),
                                    m_constantTableForUnboxedConstants.begin(),
                                    m_constantTableForUnboxedConstants.end());

        if (m_finalConstantTable.size() >= 32768)
        {
            // TODO: handle this gracefully
            //
            fprintf(stderr, "[TODO][DFG] Too many constants (>=32768) in the constant table when compiling a function! "
                            "Need to handle this gracefully, but it's not done yet so abort now.\n");
            abort();
        }

        for (Node* node : m_unboxedConstantsToFix)
        {
            TestAssert(node->IsUnboxedConstantNode());
            int64_t ord = node->GetOrdInConstantTable();
            TestAssert(ord < 0 && -static_cast<int64_t>(m_constantTableForUnboxedConstants.size()) <= ord);
            ord -= static_cast<int64_t>(m_numBoxedConstants);
            node->ModifyConstantNodeOrdInConstantTable(ord);
        }

        m_constantTableForUnboxedConstants.clear();
        m_unboxedConstantsToFix.clear();
    }

    void ALWAYS_INLINE RunPassImpl()
    {
        TestAssert(m_numBoxedConstants == static_cast<uint32_t>(-1));

#ifdef TESTBUILD
        // Assert that no constant right now should have been assigned a constant table ordinal
        //
        m_graph->ForEachBoxedConstantNode(
            [&](Node* node) ALWAYS_INLINE
            {
                TestAssert(!node->IsOrdInConstantTableAssigned());
            });
        m_graph->ForEachUnboxedConstantNode(
            [&](Node* node) ALWAYS_INLINE
            {
                TestAssert(!node->IsOrdInConstantTableAssigned());
            });
#endif

        // Initialize information for all inlined call frames
        //
        size_t numInlinedCallFrames = m_graph->GetNumInlinedCallFrames();
        InlinedFramePhysicalSlotInfo* frameInfos = m_tempAlloc.AllocateArrayUninitialized<InlinedFramePhysicalSlotInfo>(numInlinedCallFrames);
        for (size_t i = 0; i < numInlinedCallFrames; i++)
        {
            InlinedCallFrame* frame = m_graph->GetInlinedCallFrameFromOrdinal(i);
            TestAssert(frame->GetInlineCallFrameOrdinal() == i);
            InlinedFramePhysicalSlotInfo* info = frameInfos + i;
            ConstructInPlace(
                info,
                m_tempAlloc,
                frame,
                [&](Node* node) ALWAYS_INLINE   // constantHandlerCallback
                {
                    TestAssert(node->IsConstantNode() || node->IsUnboxedConstantNode());
                    ReserveSlotInConstantTable(node);
                });

            if (!frame->IsRootFrame())
            {
                uint32_t parentFrameOrd = frame->GetParentFrame()->GetInlineCallFrameOrdinal();
                TestAssert(parentFrameOrd < numInlinedCallFrames);
                TestAssert(parentFrameOrd < i);
                info->m_parentFrame = frameInfos + parentFrameOrd;
            }
        }

        auto getInfoAddrFromLocalIdent = [&](const Node::LocalIdentifier& ident) ALWAYS_INLINE WARN_UNUSED -> uint32_t*
        {
            uint32_t icfOrd = ident.m_inlinedCallFrame->GetInlineCallFrameOrdinal();
            TestAssert(icfOrd < numInlinedCallFrames);
            return frameInfos[icfOrd].GetInfoAddrForLocation(ident.m_location);
        };

        TempVector<std::pair<Node*, uint32_t* /*infoAddr*/>> localOps(m_tempAlloc);

        for (BasicBlock* bb : m_graph->m_blocks)
        {
            for (Node* node : bb->m_nodes)
            {
                // Collect all constants used by the nodes
                //
                node->ForEachInputEdge(
                    [&](Edge& e) ALWAYS_INLINE
                    {
                        Node* op = e.GetOperand();
                        if (op->IsConstantNode() || op->IsUnboxedConstantNode())
                        {
                            ReserveSlotInConstantTable(op);
                        }
                    });

                if (node->IsSetLocalNode() || node->IsGetLocalNode())
                {
                    TestAssert(node->GetBuiltinNodeInlinedNsdRefAs<uint64_t>() == static_cast<uint64_t>(-1));

                    Node::LocalIdentifier ident = node->GetLocalOperationLocalIdentifier();
                    uint32_t* infoAddr = getInfoAddrFromLocalIdent(ident);
                    TestAssert(*infoAddr == 0 || *infoAddr == 1);
                    localOps.push_back(std::make_pair(node, infoAddr));

                    // If this is a SetLocal, mark the <CallFrame, FrameLoc> as used
                    //
                    if (node->IsSetLocalNode())
                    {
                        *infoAddr = 1;
                    }
                }
            }
        }

#ifdef TESTBUILD
        // Assert that no GetLocal access locations that is not marked as used
        // These GetLocal will always see UndefValue, and should have been replaced by UndefValue
        // by block-local SSA construction pass
        //
        for (auto& it : localOps)
        {
            TestAssert(*it.second == 1);
        }
#endif

        // At this point we should have marked all needed constants
        //
        FinalizeConstantTable();

        // Compute layout for the DfgInlinedCallFrameOsrInfo data block
        //
        {
            m_inlineFrameOsrInfoOffsets = m_resultAlloc.AllocateArray<uint32_t>(numInlinedCallFrames);
            uint32_t curOffset = 0;
            for (size_t i = 0; i < numInlinedCallFrames; i++)
            {
                TestAssert(curOffset % alignof(DfgInlinedCallFrameOsrInfo) == 0);
                m_inlineFrameOsrInfoOffsets[i] = curOffset;
                curOffset += DfgInlinedCallFrameOsrInfo::GetAllocationLength(frameInfos[i].m_frameLength);
            }
            TestAssert(curOffset % alignof(DfgInlinedCallFrameOsrInfo) == 0);
            m_inlineFrameOsrInfoDataSize = curOffset;
            m_inlineFrameOsrInfoData = reinterpret_cast<uint8_t*>(m_resultAlloc.AllocateWithAlignment(
                alignof(DfgInlinedCallFrameOsrInfo), m_inlineFrameOsrInfoDataSize));
        }

        // Assign physical slot for each used <CallFrame, FrameLoc> location
        //
        VRegAwarePhysicalSlotFreeList vrapFreeList(m_tempAlloc, m_graph->GetTotalNumLocals());

        m_numTotalPhysicalSlots = 0;
        for (size_t inlinedCallFrameOrd = 0; inlinedCallFrameOrd < numInlinedCallFrames; inlinedCallFrameOrd++)
        {
            InlinedFramePhysicalSlotInfo* info = &frameInfos[inlinedCallFrameOrd];
            InlinedCallFrame* frame = info->m_frameInfo;
            TestAssertIff(inlinedCallFrameOrd == 0, frame->IsRootFrame());
            if (inlinedCallFrameOrd == 0)
            {
                info->m_freePhysicalSlots.clear();
                info->m_totalPhysicalSlots = m_graph->GetFirstDfgPhysicalSlotForLocal();
                TestAssert(info->m_totalPhysicalSlots >= 2);
            }
            else
            {
                // Free physical slots in the parent frame that are dead or occupied by this frame
                //
                InlinedCallFrame* parentFrame = frame->GetParentFrame();
                size_t parentFrameStart = parentFrame->IsRootFrame() ? 0 : parentFrame->GetInterpreterSlotForFrameStart().Value();
                size_t curFrameStart = frame->GetInterpreterSlotForFrameStart().Value();
                TestAssert(parentFrameStart <= curFrameStart);
                size_t parentFrameEndSlot = curFrameStart - parentFrameStart;
                InlinedFramePhysicalSlotInfo* parentFrameInfo = info->m_parentFrame;
                TestAssert(parentFrameInfo != nullptr);
                size_t parentFrameLength = parentFrameInfo->m_frameLength;
                TestAssert(parentFrameEndSlot <= parentFrameLength);

                auto getVregForSlotInParentFrame = [&](size_t idx) ALWAYS_INLINE WARN_UNUSED -> VirtualRegister
                {
                    TestAssert(idx < parentFrameInfo->m_frameLength);
                    TestAssert(parentFrameInfo->IsSlotLiveAndMapped(idx));
                    if (idx < parentFrameInfo->m_stackBaseIdx)
                    {
                        InterpreterSlot islot(parentFrameStart + idx);
                        VirtualRegisterMappingInfo vrmi = parentFrame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(islot);
                        TestAssert(vrmi.IsLive());
                        TestAssert(!vrmi.IsUmmapedToAnyVirtualReg());
                        return vrmi.GetVirtualRegister();
                    }
                    else
                    {
                        VirtualRegister vreg = parentFrame->GetRegisterForLocalOrd(idx - parentFrameInfo->m_stackBaseIdx);
                        return vreg;
                    }
                };

                // Everything between [parentFrameEndSlot, info->m_parentFrame->m_frameLength)
                // and everything < parentFrameEndSlot that are dead can be freed
                //
                TestAssert(vrapFreeList.IsInCleanState());
                for (size_t idx = 0; idx < parentFrameEndSlot; idx++)
                {
                    if (parentFrameInfo->IsSlotLiveAndMapped(idx))
                    {
                        // This is a slot in the parent frame. We can reuse its physical slot if it's dead now
                        //
                        size_t islot = parentFrameStart + idx;
                        VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(InterpreterSlot(islot));
                        if (!vrmi.IsLive())
                        {
                            VirtualRegister vreg = getVregForSlotInParentFrame(idx);
                            uint32_t vregSlot = SafeIntegerCast<uint32_t>(vreg.Value());
                            uint32_t physicalSlot = parentFrameInfo->m_info[idx];
                            TestAssert(physicalSlot < parentFrameInfo->m_totalPhysicalSlots);
                            vrapFreeList.AddFree(vregSlot, physicalSlot);
                        }
                    }
                }
                for (size_t idx = parentFrameEndSlot; idx < parentFrameLength; idx++)
                {
                    // This is a slot that now belongs to the current frame. It is trivially dead in the parent frame now.
                    //
                    if (parentFrameInfo->IsSlotLiveAndMapped(idx))
                    {
                        VirtualRegister vreg = getVregForSlotInParentFrame(idx);
                        uint32_t vregSlot = SafeIntegerCast<uint32_t>(vreg.Value());
                        uint32_t physicalSlot = parentFrameInfo->m_info[idx];
                        TestAssert(physicalSlot < parentFrameInfo->m_totalPhysicalSlots);
                        vrapFreeList.AddFree(vregSlot, physicalSlot);
                    }
                }

                auto getVregForSlotInCurrentFrame = [&](size_t idx) ALWAYS_INLINE WARN_UNUSED -> VirtualRegister
                {
                    TestAssert(idx < info->m_frameLength);
                    TestAssert(info->m_info[idx] == 1);
                    if (idx < info->m_stackBaseIdx)
                    {
                        InterpreterSlot islot(curFrameStart + idx);
                        VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(islot);
                        TestAssert(vrmi.IsLive());
                        TestAssert(!vrmi.IsUmmapedToAnyVirtualReg());
                        return vrmi.GetVirtualRegister();
                    }
                    else
                    {
                        VirtualRegister vreg = frame->GetRegisterForLocalOrd(idx - info->m_stackBaseIdx);
                        return vreg;
                    }
                };

                // Attempt to assign the same physical slot for the same VirtualRegister ordinal in the parent frame
                // and current frame, so that some SetLocals can be made physically no-op if possible
                //
                size_t curFrameLength = info->m_frameLength;
                for (size_t idx = 0; idx < curFrameLength; idx++)
                {
                    if (info->m_info[idx] == 1)
                    {
                        VirtualRegister vreg = getVregForSlotInCurrentFrame(idx);
                        uint32_t vregOrd = SafeIntegerCast<uint32_t>(vreg.Value());
                        if (vrapFreeList.VRegAvailable(vregOrd))
                        {
                            uint32_t physicalSlot = vrapFreeList.ConsumePhySlotForVReg(vregOrd);
                            TestAssert(physicalSlot >= 2);
                            TestAssert(physicalSlot < parentFrameInfo->m_totalPhysicalSlots);
                            info->m_info[idx] = physicalSlot;
                        }
                    }
                }

                // Build up the free list of all remaining available physical regs now
                //
                info->m_freePhysicalSlots = parentFrameInfo->m_freePhysicalSlots;
                info->m_totalPhysicalSlots = parentFrameInfo->m_totalPhysicalSlots;
                vrapFreeList.AddAllRemainingPhySlotsToFreeList(info->m_freePhysicalSlots /*out*/);
                TestAssert(vrapFreeList.IsInCleanState());
            }

#ifdef TESTBUILD
            // For sanity, assert that the free list is pairwise distinct, and does not contain anything already allocated
            //
            {
                TestAssert(info->m_totalPhysicalSlots != static_cast<uint32_t>(-1));
                TempUnorderedSet<uint32_t> chkUnique(m_tempAlloc);
                for (uint32_t val : info->m_freePhysicalSlots)
                {
                    TestAssert(val < info->m_totalPhysicalSlots);
                    TestAssert(!chkUnique.count(val));
                    chkUnique.insert(val);
                }
                for (size_t idx = 0; idx < info->m_frameLength; idx++)
                {
                    if (static_cast<int32_t>(info->m_info[idx]) > 1)
                    {
                        TestAssert(info->m_info[idx] < info->m_totalPhysicalSlots);
                        TestAssert(!chkUnique.count(info->m_info[idx]));
                        chkUnique.insert(info->m_info[idx]);
                    }
                }
            }
#endif

            size_t frameLength = info->m_frameLength;
            DfgInlinedCallFrameOsrInfo* dstInfo = std::launder(
                reinterpret_cast<DfgInlinedCallFrameOsrInfo*>(m_inlineFrameOsrInfoData + m_inlineFrameOsrInfoOffsets[inlinedCallFrameOrd]));

            ConstructInPlace(dstInfo);

            // Populate base fields in 'dstInfo'
            //
            {
                if (frame->IsRootFrame())
                {
                    dstInfo->m_parentFrameOsrInfo = 0;
                }
                else
                {
                    uint32_t parentFrameOrd = frame->GetParentFrame()->GetInlineCallFrameOrdinal();
                    TestAssert(parentFrameOrd < numInlinedCallFrames);
                    TestAssert(parentFrameOrd < inlinedCallFrameOrd);
                    uint32_t dstOffset = m_inlineFrameOsrInfoOffsets[parentFrameOrd];
                    int32_t diff = static_cast<int32_t>(dstOffset - m_inlineFrameOsrInfoOffsets[inlinedCallFrameOrd]);
                    dstInfo->m_parentFrameOsrInfo = diff;
                }

                dstInfo->m_frameStartSlot = SafeIntegerCast<uint16_t>(info->m_absoluteInterpreterSlotStart);
                dstInfo->m_frameBaseSlot = SafeIntegerCast<uint16_t>(info->m_absoluteInterpreterSlotStart + info->m_stackBaseIdx);
                dstInfo->m_frameFullLength = SafeIntegerCast<uint16_t>(info->m_frameLength);
            }

            // Assign physical slot / constant table ordinal for each slot
            //
            for (size_t idx = 0; idx < frameLength; idx++)
            {
                if (info->m_info[idx] == 1)
                {
                    if (info->m_freePhysicalSlots.size() == 0)
                    {
                        info->m_info[idx] = info->m_totalPhysicalSlots;
                        info->m_totalPhysicalSlots++;
                    }
                    else
                    {
                        info->m_info[idx] = info->m_freePhysicalSlots.back();
                        info->m_freePhysicalSlots.pop_back();
                    }
                }
                else if (info->m_info[idx] & (1U << 31))
                {
                    Node* node = ArenaPtr<Node>(info->m_info[idx] ^ (1U << 31));
                    TestAssert(node->IsConstantNode() || node->IsUnboxedConstantNode());
                    // We must have finalized the constant table, or the constant ordinal may be incorrect!
                    //
                    TestAssert(m_numBoxedConstants != static_cast<uint32_t>(-1));
                    int64_t cstTableOrd = node->GetOrdInConstantTable();
                    TestAssert(cstTableOrd < 0);
                    info->m_info[idx] = static_cast<uint32_t>(SafeIntegerCast<int32_t>(cstTableOrd));
                }

                int32_t value = static_cast<int32_t>(info->m_info[idx]);
                TestAssertImp(value > 0, m_graph->GetFirstDfgPhysicalSlotForLocal() <= static_cast<uint32_t>(value) && static_cast<uint32_t>(value) < info->m_totalPhysicalSlots);
                TestAssertImp(value < 0, value >= -static_cast<int64_t>(m_finalConstantTable.size()));

                dstInfo->m_values[idx] = SafeIntegerCast<int16_t>(value);
            }

            m_numTotalPhysicalSlots = std::max(m_numTotalPhysicalSlots, info->m_totalPhysicalSlots);

#ifdef TESTBUILD
            // For sanity, assert that all assigned physical slots are distinct
            //
            {
                TempUnorderedSet<uint32_t> chkUnique(m_tempAlloc);
                for (size_t idx = 0; idx < frameLength; idx++)
                {
                    int16_t val = dstInfo->m_values[idx];
                    if (val > 0)
                    {
                        uint32_t ord = static_cast<uint32_t>(val);
                        TestAssert(ord >= m_graph->GetFirstDfgPhysicalSlotForLocal() && ord < info->m_totalPhysicalSlots);
                        TestAssert(!chkUnique.count(ord));
                        chkUnique.insert(ord);
                    }
                }

            }
#endif
        }

        TestAssert(m_numTotalPhysicalSlots >= m_graph->GetFirstDfgPhysicalSlotForLocal());

        if (m_numTotalPhysicalSlots >= 32768)
        {
            // TODO: handle this gracefully
            //
            fprintf(stderr, "[TODO][DFG] Too many physical slots (>=32768) needed when compiling a function! "
                            "Need to handle this gracefully, but it's not done yet so abort now.\n");
            abort();
        }

        // Set up the assigned physical slot for each GetLocal and SetLocal
        //
        for (auto& it : localOps)
        {
            Node* node = it.first;
            TestAssert(node->IsGetLocalNode() || node->IsSetLocalNode());
            uint32_t* infoAddr = it.second;
            TestAssert(static_cast<int32_t>(*infoAddr) > 0);
            TestAssert(*infoAddr >= m_graph->GetFirstDfgPhysicalSlotForLocal() && *infoAddr < m_numTotalPhysicalSlots);
            node->GetBuiltinNodeInlinedNsdRefAs<uint64_t>() = *infoAddr;
        }

#ifdef TESTBUILD
        // For sanity, validate post-condition
        //
        {
            // Mapping that validates that each <CallFrame, FrameLoc> is consistently mapped to one physical slot
            //
            TempMap<std::pair<InlinedCallFrame*, int32_t>, uint64_t> phySlotMap(m_tempAlloc);

            // Validate that all Constant and UnboxedConstant nodes have valid ConstantTableOrdinal,
            // and that all SetLocal and GetLocal nodes have valid physical slot
            //
            for (BasicBlock* bb : m_graph->m_blocks)
            {
                for (Node* node : bb->m_nodes)
                {
                    node->ForEachInputEdge(
                        [&](Edge& e) ALWAYS_INLINE
                        {
                            Node* op = e.GetOperand();
                            if (op->IsConstantNode() || op->IsUnboxedConstantNode())
                            {
                                // Every Constant or UnboxedConstant node must have been assigned a valid ConstantTableOrdinal
                                //
                                TestAssert(op->IsOrdInConstantTableAssigned());
                                int64_t ord = op->GetOrdInConstantTable();
                                TestAssert(-static_cast<int64_t>(m_finalConstantTable.size()) <= ord && ord < 0);
                                TestAssertIff(op->IsConstantNode(), -static_cast<int64_t>(static_cast<uint64_t>(m_numBoxedConstants)) <= ord);
                            }
                        });

                    if (node->IsSetLocalNode() || node->IsGetLocalNode())
                    {
                        // Every GetLocal and SetLocal node must have been assigned a valid physical slot
                        //
                        uint64_t physicalSlot = node->GetBuiltinNodeInlinedNsdRefAs<uint64_t>();
                        TestAssert(physicalSlot != static_cast<uint64_t>(-1));
                        TestAssert(m_graph->GetFirstDfgPhysicalSlotForLocal() <= physicalSlot && physicalSlot < m_numTotalPhysicalSlots);

                        Node::LocalIdentifier ident = node->GetLocalOperationLocalIdentifier();
                        std::pair<InlinedCallFrame*, int32_t> key = std::make_pair(ident.m_inlinedCallFrame, ident.m_location.GetRawValue());
                        if (phySlotMap.count(key))
                        {
                            TestAssert(phySlotMap[key] == physicalSlot);
                        }
                        else
                        {
                            phySlotMap[key] = physicalSlot;
                        }
                    }
                }
            }

            // Reconstruct the base interpreter frames mapping using each DfgInlinedCallFrameOsrInfo,
            // and cross-check it with the InlinedCallFrame to make sure everything looks reasonable
            //
            uint64_t* fakeDfgLocals = m_tempAlloc.AllocateArray<uint64_t>(m_numTotalPhysicalSlots);
            for (size_t i = 0; i < m_numTotalPhysicalSlots; i++)
            {
                fakeDfgLocals[i] = i;
            }
            uint64_t* fakeConstantTable = m_tempAlloc.AllocateArray<uint64_t>(m_finalConstantTable.size());
            for (size_t i = 0; i < m_finalConstantTable.size(); i++)
            {
                fakeConstantTable[i] = i - m_finalConstantTable.size();
            }
            uint64_t* fakeConstantTableEnd = fakeConstantTable + m_finalConstantTable.size();

            for (size_t inlinedCallFrameOrd = 0; inlinedCallFrameOrd < m_graph->GetNumInlinedCallFrames(); inlinedCallFrameOrd++)
            {
                DfgInlinedCallFrameOsrInfo* info = std::launder(
                    reinterpret_cast<DfgInlinedCallFrameOsrInfo*>(m_inlineFrameOsrInfoData + m_inlineFrameOsrInfoOffsets[inlinedCallFrameOrd]));

                for (size_t i = 0; i < info->m_frameFullLength; i++)
                {
                    TestAssert(-static_cast<int64_t>(m_finalConstantTable.size()) <= info->m_values[i]);
                    TestAssert(info->m_values[i] < static_cast<int64_t>(m_numTotalPhysicalSlots));
                }

                InlinedCallFrame* frame = m_graph->GetInlinedCallFrameFromOrdinal(inlinedCallFrameOrd);

                size_t numSlots = info->GetInterpreterFramesTotalNumSlots();
                TestAssert(numSlots == frame->GetInterpreterSlotForStackFrameBase().Value() + frame->GetNumBytecodeLocals());

                uint64_t* content = m_tempAlloc.AllocateArray<uint64_t>(numSlots);
                for (size_t i = 0; i < numSlots; i++) { content[i] = static_cast<uint64_t>(1) << 63; }

                info->ReconstructInterpreterFramesBaseInfo(fakeConstantTableEnd, fakeDfgLocals, content /*out*/);

                for (size_t i = 0; i < numSlots; i++)
                {
                    TestAssert(content[i] != static_cast<uint64_t>(1) << 63);
                    int64_t value = static_cast<int64_t>(content[i]);
                    if (value > 0)
                    {
                        TestAssert(m_graph->GetFirstDfgPhysicalSlotForLocal() <= content[i] && content[i] < m_numTotalPhysicalSlots);
                    }
                    else if (value < 0)
                    {
                        TestAssert(-static_cast<int64_t>(m_finalConstantTable.size()) <= value);
                        // It must be the case that InlinedCallFrame says the slot is holding a statically-known constant value with the same content
                        //
                        TestAssert(i < frame->GetInterpreterSlotForStackFrameBase().Value());
                        VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(InterpreterSlot(i));
                        TestAssert(vrmi.IsLive());
                        TestAssert(vrmi.IsUmmapedToAnyVirtualReg());
                        TestAssert(vrmi.GetConstantValue()->GetOrdInConstantTable() == value);
                    }
                }

                // All slots that InlinedCallFrame says is holding a statically-known constant should also be constant in the reconstructed stack
                //
                TestAssert(numSlots >= frame->GetInterpreterSlotForStackFrameBase().Value());
                for (size_t i = 0; i < frame->GetInterpreterSlotForStackFrameBase().Value(); i++)
                {
                    VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(InterpreterSlot(i));
                    if (vrmi.IsLive() && vrmi.IsUmmapedToAnyVirtualReg())
                    {
                        // Equality has been checked in the earlier loop, so only needs to check <0
                        //
                        TestAssert(static_cast<int64_t>(content[i]) < 0);
                    }
                }

                TempUnorderedSet<size_t> checkPhySlotUnique(m_tempAlloc);
                for (size_t i = 0; i < numSlots; i++)
                {
                    // Note that it is possible that a slot holds 0 (i.e., does not map to a DFG physical slot) in the reconstructed
                    // frames but is live in InlinedCallFrame, if the slot borns and dies within the same DFG basic block
                    // (so no GetLocal/SetLocal ever operates on it)
                    //
                    if (static_cast<int64_t>(content[i]) > 0)
                    {
                        // If InlinedCallFrame says the slot is dead, it may hold any value.
                        //
                        if (i < frame->GetInterpreterSlotForStackFrameBase().Value())
                        {
                            VirtualRegisterMappingInfo vrmi = frame->GetVirtualRegisterInfoForInterpreterSlotBeforeFrameBase(InterpreterSlot(i));
                            if (!vrmi.IsLive())
                            {
                                // InlinedCallFrame says the slot is dead, no need to check anything
                                //
                                continue;
                            }
                            // But if the slot is live, InlinedCallFrame should not claim that it's holding a constant value
                            //
                            TestAssert(!vrmi.IsUmmapedToAnyVirtualReg());
                        }

                        // Now we know the slot is mapped to a valid DFG physical slot, and is also live according to InlinedCallFrame
                        // All such physical slots must be pairwise distinct
                        //
                        TestAssert(!checkPhySlotUnique.count(content[i]));
                        checkPhySlotUnique.insert(content[i]);
                    }
                }

                // Assert that the PhysicalSlotFreeList indeed complements the active physical slots
                //
                InlinedFramePhysicalSlotInfo* frameInfo = &frameInfos[inlinedCallFrameOrd];
                for (uint32_t value : frameInfo->m_freePhysicalSlots)
                {
                    TestAssert(!checkPhySlotUnique.count(value));
                    checkPhySlotUnique.insert(value);
                }

                TestAssert(m_graph->GetFirstDfgPhysicalSlotForLocal() <= frameInfo->m_totalPhysicalSlots);
                TestAssert(checkPhySlotUnique.size() == frameInfo->m_totalPhysicalSlots - m_graph->GetFirstDfgPhysicalSlotForLocal());
                for (size_t value = m_graph->GetFirstDfgPhysicalSlotForLocal(); value < frameInfo->m_totalPhysicalSlots; value++)
                {
                    TestAssert(checkPhySlotUnique.count(value));
                }
            }
        }
#endif
    }

    TempArenaAllocator& m_resultAlloc;
    TempArenaAllocator m_tempAlloc;
    TempVector<uint64_t> m_finalConstantTable;
    TempVector<uint64_t> m_constantTableForUnboxedConstants;
    TempVector<Node*> m_unboxedConstantsToFix;
    uint32_t m_numBoxedConstants;
    uint32_t m_numTotalPhysicalSlots;
    uint32_t* m_inlineFrameOsrInfoOffsets;
    uint8_t* m_inlineFrameOsrInfoData;
    uint32_t m_inlineFrameOsrInfoDataSize;
    Graph* m_graph;
};

}   // anonymous namespace

StackLayoutPlanningResult WARN_UNUSED RunStackLayoutPlanningPass(TempArenaAllocator& resultAlloc, Graph* graph)
{
    return StackLayoutPlanningPass::RunPass(resultAlloc, graph);
}

}   // namespace dfg
