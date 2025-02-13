#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"
#include "sorting_network.h"
#include "dfg_reg_alloc_node_info.h"

namespace dfg {

// Descriptor for codegen the main node logic
//
struct RegAllocMainCodegenDesc
{
    RegAllocMainCodegenDesc()
        : m_outputVal(nullptr)
        , m_brDecisionVal(nullptr)
        , m_numOperands(0)
        , m_hasOutput(false)
        , m_outputMayReuseInputReg(false)
        , m_outputPrefersReuseInputReg(false)
        , m_hasBrDecisionOutput(false)
        , m_brDecisionMayReuseInputReg(false)
        , m_brDecisionPrefersReuseInputReg(false)
        , m_shouldTurnAllGprGroup1RegsToScratch(false)
    { }

    // The fixedOperands of the node
    //
    ValueUseRAInfo m_operands[8];

    // The output and brDecision SSA value, if exists
    //
    ValueRegAllocInfo* m_outputVal;
    ValueRegAllocInfo* m_brDecisionVal;

    uint8_t m_numOperands;

    // Output information:
    //     m_hasOutput: whether the node has an output
    //     m_outputMayReuseInputReg: whether the output is allowed to take the position of an input reg
    //     m_outputPrefersReuseInputReg: whether it would produce better code if the output takes the position of an input reg
    //
    // A bit more explanation on m_outputPrefersReuseInputReg:
    //     When it is true, it means the code can be better (but at most better by having one less mov, since the code itself
    //     can always use a mov to move the input reg to another location) if the output reuses one of the input regs.
    //     In that case, if we know an input reg will be spilled before next use, we can let the output take that input reg,
    //     so we reduced one mov in the code.
    //     Currently we simply use 'nextSpillEverythingIndex' to decide if we know for sure an SSA value will be spilled
    //     before next use: this is probably the best we can do as a one-pass reg alloc.
    //
    bool m_hasOutput;
    bool m_outputMayReuseInputReg;
    bool m_outputPrefersReuseInputReg;

    // BranchDecisionOutput information, similar to output.
    // Note that if there is an output, 'm_branchDecisionMayReuseInputReg' may only be true if 'm_outputMayReuseInputReg' is also true
    //
    bool m_hasBrDecisionOutput;
    bool m_brDecisionMayReuseInputReg;
    bool m_brDecisionPrefersReuseInputReg;

    // For some nodes, one can get better overall code by making all group-1 GPR into scratch registers
    // (generating explicit moves as required) since it avoid collision with C calling convention.
    //
    // 'shouldRelocateAllGprGroup1Regs' indicates if this is the case. True if we should explicitly
    // make all Group1 GPR registers scratch registers by generating moves
    //
    bool m_shouldTurnAllGprGroup1RegsToScratch;

    // Number of scratch registers the node needs
    //
    uint8_t m_numScratchRegistersRequired;

    // The next useIndex we know that we will spill everything
    // If the next use of an SSA value >= this index, we know it will be spilled before being used next time
    //
    uint32_t m_nextSpillEverythingIndex;

    // The useIndex of the node, for assertion purpose
    //
    uint32_t m_useIndex;
};

struct RegAllocPassthruAndScratchRegInfo
{
    // The index mask for the scratch and passthrough registers
    // For FPR, only group1 is populated since we never use group2 FPR
    //
    uint8_t m_group1ScratchIdxMask;
    uint8_t m_group1PassthruIdxMask;
    uint8_t m_group2ScratchIdxMask;
    uint8_t m_group2PassthruIdxMask;

    void InitForGprState(uint8_t numScratchRegs, uint16_t scratchMask, uint16_t passthruMask, size_t numDesiredScratchRegs)
    {
        TestAssert(CountNumberOfOnes(scratchMask) == numScratchRegs);
        TestAssert(scratchMask < (1U << x_dfg_reg_alloc_num_gprs));
        TestAssert(passthruMask < (1U << x_dfg_reg_alloc_num_gprs));
        TestAssert((scratchMask & passthruMask) == 0);

        m_group1ScratchIdxMask = static_cast<uint8_t>(scratchMask >> (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs));
        m_group2ScratchIdxMask = static_cast<uint8_t>(scratchMask & ((1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)) - 1));

        m_group1PassthruIdxMask = static_cast<uint8_t>(passthruMask >> (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs));
        m_group2PassthruIdxMask = static_cast<uint8_t>(passthruMask & ((1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)) - 1));

        TestAssert((m_group1ScratchIdxMask & m_group1PassthruIdxMask) == 0);
        TestAssert((m_group2ScratchIdxMask & m_group2PassthruIdxMask) == 0);

        // The codegen expects an exact number of scratch regs and passthru regs
        // So if we have more scratch regs than expected, we should treat some of the scratch regs as passthru regs
        //
        TestAssert(numScratchRegs >= numDesiredScratchRegs);
        if (numScratchRegs > numDesiredScratchRegs)
        {
            size_t numToChange = numScratchRegs - numDesiredScratchRegs;
            size_t numGroup2Scratches = CountNumberOfOnes(m_group2ScratchIdxMask);
            if (numGroup2Scratches < numToChange)
            {
                // Change all group2 scratch to passthru, then change the remaining desired # of group1 scratch to passthru
                //
                m_group2PassthruIdxMask |= m_group2ScratchIdxMask;
                m_group2ScratchIdxMask = 0;
                size_t numGroup1ScratchToChange = numToChange - numGroup2Scratches;
                TestAssert(CountNumberOfOnes(m_group1ScratchIdxMask) >= numGroup1ScratchToChange);
                uint8_t group1ScMaskToChange = GetFirstKOnesInUint8Val(m_group1ScratchIdxMask, numGroup1ScratchToChange);
                TestAssert((m_group1ScratchIdxMask & group1ScMaskToChange) == group1ScMaskToChange);
                m_group1ScratchIdxMask ^= group1ScMaskToChange;
                m_group1PassthruIdxMask |= group1ScMaskToChange;
            }
            else
            {
                // Change the necessary number of group2 scratch to passthru
                // It is necessary that we don't touch group1, since in some cases, caller may expect all group1 to be scratch
                //
                uint8_t group2ScMaskToChange = GetFirstKOnesInUint8Val(m_group2ScratchIdxMask, numToChange);
                TestAssert((m_group2ScratchIdxMask & group2ScMaskToChange) == group2ScMaskToChange);
                m_group2ScratchIdxMask ^= group2ScMaskToChange;
                m_group2PassthruIdxMask |= group2ScMaskToChange;
            }
        }

        TestAssert(m_group1ScratchIdxMask < (1U << x_dfg_reg_alloc_num_group1_gprs));
        TestAssert(m_group1PassthruIdxMask < (1U << x_dfg_reg_alloc_num_group1_gprs));
        TestAssert(m_group2ScratchIdxMask < (1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)));
        TestAssert(m_group2PassthruIdxMask < (1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)));
        TestAssert((m_group1ScratchIdxMask & m_group1PassthruIdxMask) == 0);
        TestAssert((m_group2ScratchIdxMask & m_group2PassthruIdxMask) == 0);
        TestAssert(CountNumberOfOnes(m_group1ScratchIdxMask) + CountNumberOfOnes(m_group2ScratchIdxMask) == numDesiredScratchRegs);
        TestAssert(numDesiredScratchRegs + CountNumberOfOnes(m_group1PassthruIdxMask) + CountNumberOfOnes(m_group2PassthruIdxMask) == numScratchRegs + CountNumberOfOnes(passthruMask));
    }

    void InitForFprState(uint8_t numScratchRegs, uint16_t scratchMask, uint16_t passthruMask, size_t numDesiredScratchRegs)
    {
        TestAssert(CountNumberOfOnes(scratchMask) == numScratchRegs);
        TestAssert(scratchMask < (1U << x_dfg_reg_alloc_num_fprs));
        TestAssert(passthruMask < (1U << x_dfg_reg_alloc_num_fprs));
        TestAssert((scratchMask & passthruMask) == 0);

        m_group1ScratchIdxMask = SafeIntegerCast<uint8_t>(scratchMask);
        m_group1PassthruIdxMask = SafeIntegerCast<uint8_t>(passthruMask);
        m_group2ScratchIdxMask = 0;
        m_group2PassthruIdxMask = 0;

        TestAssert(numScratchRegs >= numDesiredScratchRegs);
        if (numScratchRegs > numDesiredScratchRegs)
        {
            size_t numToChange = numScratchRegs - numDesiredScratchRegs;
            uint8_t maskToChange = GetFirstKOnesInUint8Val(m_group1ScratchIdxMask, numToChange);
            TestAssert((m_group1ScratchIdxMask & maskToChange) == maskToChange);
            m_group1ScratchIdxMask ^= maskToChange;
            m_group1PassthruIdxMask |= maskToChange;
        }

        TestAssert(m_group1ScratchIdxMask < (1U << x_dfg_reg_alloc_num_fprs));
        TestAssert(m_group1PassthruIdxMask < (1U << x_dfg_reg_alloc_num_fprs));
        TestAssert((m_group1ScratchIdxMask & m_group1PassthruIdxMask) == 0);
        TestAssert(CountNumberOfOnes(m_group1ScratchIdxMask) == numDesiredScratchRegs);
        TestAssert((m_group1ScratchIdxMask | m_group1PassthruIdxMask) == (scratchMask | passthruMask));
    }
};

struct RegAllocTypecheckCodegenDecisions
{
    uint8_t m_inputRegIdx;
    RegAllocPassthruAndScratchRegInfo m_nonOperandRegInfo;
};

// Result returned by WorkForCodegen
//
struct RegAllocMainCodegenDecisions
{
    bool IsInputOrdReusedForOutputOrBrDecision(size_t ord)
    {
        if (m_outputReusedInputRegister && ord == m_outputReusedInputOperandOrd) { return true; }
        if (m_brDecisionReusedInputRegister && ord == m_brDecisionReusedInputOperandOrd) { return true; }
        return false;
    }

    uint8_t GetInputRegIdx(size_t inputOrdInThisRegBank)
    {
        TestAssert(inputOrdInThisRegBank < m_numInputRegsInThisBank);
        return m_inputRegIdx[inputOrdInThisRegBank];
    }

    uint8_t m_inputRegIdx[8];
    uint8_t m_numInputRegsInThisBank;
    RegAllocPassthruAndScratchRegInfo m_nonOperandRegInfo;
    // Whether or not we decided output should take the position of an input reg
    //
    bool m_outputReusedInputRegister;
    // If the output reused an input reg, this is the operand ordinal of m_operands
    //
    uint8_t m_outputReusedInputOperandOrd;
    // The register index where the output is stored in
    //
    uint8_t m_outputRegIdx;
    // Similar decision info but for brDecisionOutput
    //
    bool m_brDecisionReusedInputRegister;
    uint8_t m_brDecisionReusedInputOperandOrd;
    uint8_t m_brDecisionRegIdx;
};

// Manage the current value stored in each register and their next use time,
// and generate the register operations to fullfil the codegen requirements.
//
// 'RegEventCallbacks' should be a class that provides the following methods:
//
//     template<bool forGprState>
//     void EvictRegister(ValueRegAllocInfo* val, size_t regIdx)
//         Called when we decide that SSA value 'val' should be evicted from regList[regIdx]
//         'regIdx' must be holding 'val' right now.
//         Note that despite being evicted, in general, the register must not be garbaged until codegen
//         of the current operation, since if an input register is also used to hold the output, we will call Evict
//         on the input reg before codegen (so the value is spilled and not lost), but the input reg must still
//         be holding the input value since the node will use it.
//
//     template<bool forGprState>
//     void RelocateRegister(ValueRegAllocInfo* val, size_t fromRegIdx, size_t toRegIdx)
//         Called when we decide that SSA value 'val' should be moved from regList[fromRegIdx] to regList[toRegIdx]
//         'fromRegIdx' must be holding 'val', and 'toIdx' must be a scratch register.
//
//     template<bool forGprState>
//     void DuplicateRegister(size_t fromRegIdx, size_t toRegIdx)
//          Called when we decide that the value in 'fromRegIdx' should be cloned to 'toRegIdx'
//          'toRegIdx' must be a scratch register, and should *still* be considered a scratch register after this operation.
//
//     template<bool forGprState>
//     void LoadRegister(ValueRegAllocInfo* val, size_t regIdx)
//         Called when we decide that SSA value 'val' should be loaded into regList[regIdx]
//         'val' must not be stored in any register now, and 'regIdx' must be a scratch register.
//
//     template<bool forGprState>
//     void KillRegister(ValueRegAllocInfo* val, size_t regIdx)
//         Called when register 'regIdx' is dead because the SSA value it is currently holding (which must be 'val') is dead
//
template<bool forGprState, typename RegEventCallbacks>
struct RegAllocDecisionMaker
{
    MAKE_NONCOPYABLE(RegAllocDecisionMaker);
    MAKE_NONMOVABLE(RegAllocDecisionMaker);

    RegAllocDecisionMaker(TempArenaAllocator& alloc, RegEventCallbacks& cbs)
        : m_cbs(cbs)
        , m_rangedOperandDuplicateEdgeList(alloc)
        , m_rangedOperandDuplicateEdgeMap(alloc)
        , m_rangedOperandOrdToHandle(alloc)
        , m_rangedOperandInfoList(alloc)
        , m_rangedOperandResultOrder(alloc)
    {
        Reset();
    }

    void Reset()
    {
        m_rangedOperandDuplicateEdgeList.clear();
        m_rangedOperandDuplicateEdgeMap.clear();
        m_rangedOperandOrdToHandle.clear();
        m_rangedOperandInfoList.clear();
        m_rangedOperandResultOrder.clear();
        {
            uint32_t val = ValueNextUseInfo::NoMoreUse().m_value | x_isScratchMask;
            for (uint32_t i = 0; i < x_numRegs; i++)
            {
                m_data[i] = val + i;
            }
        }
        for (uint8_t i = 0; i < x_numRegs; i++)
        {
            m_regIdxToDataIdx[i] = i;
        }
        m_isSorted = true;
        m_numScratchRegs = x_numRegs;
        m_scratchRegMask = static_cast<uint16_t>((1U << x_numRegs) - 1);
        m_reservedRegMask = 0;
        m_outputRegMask = 0;
        for (size_t i = 0; i < x_numRegs; i++)
        {
            m_regValues[i] = nullptr;
        }
        AssertConsistency();
    }

    // Sort everything based on next use
    //
    void SortRegisters()
    {
        if (!m_isSorted)
        {
            AssertConsistency();

            SortingNetwork::SortAscend<x_numRegs>(m_data);

            // Update the mapping on where each register in located in the sorted sequence
            //
#ifdef TESTBUILD
            for (size_t i = 0; i < x_numRegs; i++) { m_regIdxToDataIdx[i] = 255; }
#endif

            for (uint8_t i = 0; i < x_numRegs; i++)
            {
                size_t regIdx = ValueNextUseInfo(m_data[i]).GetCustomDataAsU8();
                TestAssert(regIdx < x_numRegs);
                m_regIdxToDataIdx[regIdx] = i;
            }

#ifdef TESTBUILD
            for (size_t i = 0; i < x_numRegs; i++) { TestAssert(m_regIdxToDataIdx[i] != 255); }
#endif

            m_isSorted = true;
        }

        AssertConsistency();
    }

    // Sanity check that we've consumed all uses correctly, so the next use of anything in register should be at least useIndex
    //
    void AssertMinimumUseIndex([[maybe_unused]] uint32_t useIndex)
    {
#ifdef TESTBUILD
        for (size_t i = 0; i < x_numRegs; i++)
        {
            ValueNextUseInfo info(m_data[i]);
            TestAssert(info.GetNextUseIndex() >= useIndex);
        }
#endif
    }

    // Update the NextUse of a register
    // The NextUse in the SSA value is *not* updated.
    //
    void ALWAYS_INLINE UpdateNextUseInfo(uint8_t regIndex, ValueUseRAInfo use)
    {
        TestAssert(!use.IsDuplicateEdge());
        TestAssert(regIndex < x_numRegs);
        uint8_t dataIdx = m_regIdxToDataIdx[regIndex];
        TestAssert(dataIdx < x_numRegs);
        TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU16() == regIndex);
        TestAssert(m_regValues[regIndex] != nullptr);
        TestAssert(GetValRegOrdInList(m_regValues[regIndex]) == regIndex);
        TestAssert((m_scratchRegMask & static_cast<uint16_t>(1U << regIndex)) == 0);
        TestAssert((m_reservedRegMask & static_cast<uint16_t>(1U << regIndex)) == 0);
        ValueNextUseInfo newInfo = use.GetNextUseInfo();
        TestAssert(newInfo.GetCustomDataAsU16() == 0);
        m_data[dataIdx] = newInfo.m_value | regIndex;
        m_isSorted = false;
    }

    // Ranged operands are used one by one, and may be used in any order
    // So the workflow is the following:
    // 1. Check which ranged operands are already in register, use them, and update their next use
    // 2. For the rest of the ranged operands, call AddNewRangedOperand to add them to a list
    // 3. Sort everything (existing regs and desired ranged operands) based on their next use
    // 4. Some ranged operands will be loaded into register, and we can use them.
    // 5. For the rest, caller should load each of them into an unused scratch register and use them.
    //
    void PrepareToProcessRangedOperands(size_t numRangedOperands)
    {
        AssertConsistency();
        m_rangedOperandDuplicateEdgeMap.clear();
        m_rangedOperandDuplicateEdgeList.clear();
        m_rangedOperandOrdToHandle.clear();
        ResizeVectorTo(m_rangedOperandOrdToHandle, numRangedOperands, 0U /*value*/);
        m_rangedOperandInfoList.clear();
    }

    // Add a ranged operand that is not already in register,
    // and update the useInfo of the SSA value to be after the use of this operand.
    //
    void AddNewRangedOperand(uint16_t ord, ValueUseRAInfo use)
    {
        TestAssert(ord < m_rangedOperandOrdToHandle.size());
        TestAssert(m_rangedOperandOrdToHandle[ord] == 0);
        // Note that the highest bit of CustomData is used by us to denote whether the register is scratch, so there's one less bit we have here
        //
        TestAssert(ord + x_numRegs < (1U << (ValueNextUseInfo::Data_CustomDataAsU16::BitWidth() - 1)));
        TestAssert(use.IsGPRUse() == IsForGprState());
        ValueRegAllocInfo* ssaVal = use.GetSSAValue();
        TestAssert(!IsValAvailableInReg(ssaVal));
        uint32_t ssaValPtr = ArenaPtr<ValueRegAllocInfo>(ssaVal).m_value;
        if (unlikely(use.IsDuplicateEdge()))
        {
            uint16_t ordInEdgeList = SafeIntegerCast<uint16_t>(m_rangedOperandDuplicateEdgeList.size());
            auto [it, inserted] = m_rangedOperandDuplicateEdgeMap.try_emplace(ssaValPtr, ordInEdgeList);
            if (inserted)
            {
                m_rangedOperandDuplicateEdgeList.push_back(std::make_pair(ord, static_cast<uint16_t>(-1)));
            }
            else
            {
                uint16_t headIdx = it->second;
                m_rangedOperandDuplicateEdgeList.push_back(std::make_pair(ord, headIdx));
                it->second = ordInEdgeList;
            }
            TestAssert(m_rangedOperandDuplicateEdgeMap.count(ssaValPtr));
            TestAssert(m_rangedOperandDuplicateEdgeMap[ssaValPtr] == ordInEdgeList);
            m_rangedOperandOrdToHandle[ord] = 1;
        }
        else
        {
            TestAssert(ssaValPtr > 1);
            m_rangedOperandOrdToHandle[ord] = ssaValPtr;
            ValueNextUseInfo info = use.GetNextUseInfo();
            TestAssert(info.GetCustomDataAsU16() == 0);
            uint32_t val = info.m_value | static_cast<uint32_t>(ord + x_numRegs);
            TestAssert(ValueNextUseInfo(val).GetCustomDataAsU16() == ord + x_numRegs);
            m_rangedOperandInfoList.push_back(val);

            UpdateValNextUseInfo(ssaVal, info);
        }
    }

    // This will call OnEviction and OnLoad as needed
    //
    // Returns the order to deal with the ranged operands.
    // It is guaranteed that all duplicate edges follow immediately by the first use,
    // and all operands that have been loaded into the register come first.
    //
    // If 'doNotEvictRegIdx' is not static_cast<uint8_t>(-1), we will guarantee to not touch the specified regIdx (even if it's a scratch reg now)
    //
    std::span<uint16_t> WARN_UNUSED ProcessRangedOperands(uint8_t doNotEvictRegIdx)
    {
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);

        AssertConsistency();

        m_isSorted = false;

        // A max heap for the registers and operands with smallest NextUse
        //
        uint32_t h[x_numRegs];
        memcpy(h, m_data, sizeof(uint32_t) * x_numRegs);

        if (doNotEvictRegIdx != static_cast<uint8_t>(-1))
        {
            TestAssert(doNotEvictRegIdx < x_numRegs);
            uint8_t dataIdx = m_regIdxToDataIdx[doNotEvictRegIdx];
            TestAssert(dataIdx < x_numRegs);
            TestAssert(ValueNextUseInfo(h[dataIdx]).GetCustomDataAsU8() == doNotEvictRegIdx);
            // Since all useIndex > 0, setting the value to 0 is enough to prevent the value from ever showing up as the max element
            //
            TestAssert(x_numRegs > 1 && "need at least 2 regs to lock one reg from eviction");
            h[dataIdx] = 0;
        }

        std::make_heap(h, h + x_numRegs);

        bool hasDuplicateEdges = (m_rangedOperandDuplicateEdgeList.size() > 0);

        constexpr uint32_t x_identMask = (1U << (ValueNextUseInfo::Data_CustomDataAsU16::BitWidth() - 1)) - 1;

        for (uint32_t infoData : m_rangedOperandInfoList)
        {
            TestAssert((infoData & x_isScratchMask) == 0);
            TestAssert((infoData & x_identMask) >= x_numRegs);

            if (infoData > h[0])
            {
                continue;
            }

            uint32_t removedElement = h[0];
            TestAssert(removedElement > 0);
            uint32_t regOrOp = removedElement & x_identMask;
            if (regOrOp < x_numRegs)
            {
                bool isScratch = ((removedElement & x_isScratchMask) > 0);
                TestAssertIff(isScratch, m_regValues[regOrOp] == nullptr);
                if (!isScratch)
                {
                    EvictRegisterImpl(static_cast<uint8_t>(regOrOp), false /*dueToDeath*/, false /*dueToTakenByOutput*/);
                }
            }
            else
            {
                TestAssert((removedElement & x_isScratchMask) == 0);
            }

            std::pop_heap(h, h + x_numRegs);
            TestAssert(h[x_numRegs - 1] == removedElement);

            h[x_numRegs - 1] = infoData;
            std::push_heap(h, h + x_numRegs);
        }

        m_rangedOperandResultOrder.clear();

        auto appendToResult = [&](uint16_t opIdent) ALWAYS_INLINE
        {
            TestAssert(opIdent < m_rangedOperandOrdToHandle.size());
            TestAssert(m_rangedOperandOrdToHandle[opIdent] > 1);
            ArenaPtr<ValueRegAllocInfo> ssaVal(m_rangedOperandOrdToHandle[opIdent]);
            m_rangedOperandOrdToHandle[opIdent] = 0;
            m_rangedOperandResultOrder.push_back(opIdent);
            if (unlikely(hasDuplicateEdges))
            {
                auto it = m_rangedOperandDuplicateEdgeMap.find(ssaVal.m_value);
                if (it != m_rangedOperandDuplicateEdgeMap.end())
                {
                    uint16_t headIdx = it->second;
                    while (true)
                    {
                        TestAssert(headIdx < m_rangedOperandDuplicateEdgeList.size());
                        auto listNode = m_rangedOperandDuplicateEdgeList[headIdx];
                        uint16_t val = listNode.first;
                        TestAssert(val < m_rangedOperandOrdToHandle.size());
                        TestAssert(m_rangedOperandOrdToHandle[val] == 1);
                        m_rangedOperandOrdToHandle[val] = 0;
                        m_rangedOperandResultOrder.push_back(val);
                        headIdx = listNode.second;
                        if (headIdx == static_cast<uint16_t>(-1))
                        {
                            break;
                        }
                    }
                }
            }
        };

        for (size_t i = 0; i < x_numRegs; i++)
        {
            uint16_t regOrOp = h[i] & x_identMask;
            if (regOrOp >= x_numRegs)
            {
                uint16_t opIdent = static_cast<uint16_t>(regOrOp - x_numRegs);
                TestAssert(opIdent < m_rangedOperandOrdToHandle.size());
                TestAssert(m_rangedOperandOrdToHandle[opIdent] > 1);
                ValueRegAllocInfo* ssaVal = ArenaPtr<ValueRegAllocInfo>(m_rangedOperandOrdToHandle[opIdent]);
                TestAssert(!IsValAvailableInReg(ssaVal));
                LoadValueToRegister(ssaVal, false /*isBornForOutput*/);
                appendToResult(opIdent);
            }
        }

        for (uint16_t opIdent = 0; opIdent < m_rangedOperandOrdToHandle.size(); opIdent++)
        {
            if (m_rangedOperandOrdToHandle[opIdent] > 1)
            {
                appendToResult(opIdent);
            }
        }

#ifdef TESTBUILD
        for (uint32_t val : m_rangedOperandOrdToHandle) { TestAssert(val == 0); }
#endif

        AssertConsistency();

        return { m_rangedOperandResultOrder.begin(), m_rangedOperandResultOrder.end() };
    }

    // Make eviction until we have at least the given number of scratch registers
    // If 'shouldAlsoRelocateAllGroup1Gprs' is true, we will also make register moves to make all Group1Gprs scratch regs
    //
    RegAllocPassthruAndScratchRegInfo WARN_UNUSED EvictUntil(size_t desiredNumScratchRegs, bool shouldAlsoRelocateAllGroup1Gprs)
    {
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);

        if (!IsForGprState())
        {
            shouldAlsoRelocateAllGroup1Gprs = false;
        }

        TestAssertImp(shouldAlsoRelocateAllGroup1Gprs, desiredNumScratchRegs >= x_dfg_reg_alloc_num_group1_gprs);
        TestAssert(desiredNumScratchRegs <= x_numRegs);

        if (desiredNumScratchRegs > 0)
        {
            SortRegisters();

            if (desiredNumScratchRegs > m_numScratchRegs)
            {
                // If there were already scratch registers,
                // m_data may no longer become sorted after eviction, since the free register ordinals may no longer be sorted
                //
                if (m_numScratchRegs > 0)
                {
                    m_isSorted = false;
                }

                size_t evictDataIdxBegin = x_numRegs - desiredNumScratchRegs;
                size_t evictDataIdxEnd = x_numRegs - m_numScratchRegs;
                for (size_t dataIdx = evictDataIdxBegin; dataIdx < evictDataIdxEnd; dataIdx++)
                {
                    TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                    uint8_t regIdx = ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8();
                    TestAssert(regIdx < x_numRegs);
                    EvictRegisterImpl(regIdx, false /*dueToDeath*/, false /*dueToTakenByOutput*/);
                }
            }

            TestAssert(m_numScratchRegs >= desiredNumScratchRegs);

            if (shouldAlsoRelocateAllGroup1Gprs)
            {
                RelocateToTurnAllGroup1GprIntoScratch();
            }
        }

        TestAssert(m_numScratchRegs >= desiredNumScratchRegs);
        AssertConsistency();

        RegAllocPassthruAndScratchRegInfo res;
        if (IsForGprState())
        {
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_gprs) - 1) ^ m_scratchRegMask);
            res.InitForGprState(m_numScratchRegs, m_scratchRegMask, passthruMask, desiredNumScratchRegs);
        }
        else
        {
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_fprs) - 1) ^ m_scratchRegMask);
            res.InitForFprState(m_numScratchRegs, m_scratchRegMask, passthruMask, desiredNumScratchRegs);
        }
        return res;
    }

    // Do necessary register operations to codegen a Check, calling OnEviction and OnLoad as needed, and update the nextUse for 'use'
    //
    // Return the regIdx for the operand and the passthru/scratch reg config
    //
    RegAllocTypecheckCodegenDecisions WARN_UNUSED WorkForCodegenCheck(ValueUseRAInfo use, size_t requiredNumScratchRegs, bool shouldAlsoRelocateAllGroup1Gprs)
    {
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);

        if (!IsForGprState())
        {
            shouldAlsoRelocateAllGroup1Gprs = false;
        }

        TestAssert(!use.IsDuplicateEdge());
        ValueRegAllocInfo* ssaVal = use.GetSSAValue();

        SortRegisters();

        TestAssertImp(shouldAlsoRelocateAllGroup1Gprs, requiredNumScratchRegs >= x_dfg_reg_alloc_num_group1_gprs);

        size_t numRegsToEvict = requiredNumScratchRegs;
        bool needLoad = false;
        uint8_t operandRegIdx = 255;
        uint8_t operandDataIdx = 255;
        if (IsValAvailableInReg(ssaVal))
        {
            operandRegIdx = GetValRegOrdInList(ssaVal);
            operandDataIdx = m_regIdxToDataIdx[operandRegIdx];
            TestAssert(operandDataIdx < x_numRegs);
            [[maybe_unused]] ValueNextUseInfo useInfo(m_data[operandDataIdx]);
            TestAssert((useInfo.m_value & x_isScratchMask) == 0);
            TestAssert(useInfo.GetCustomDataAsU8() == operandRegIdx);
            TestAssert(useInfo.GetNextUseIndex() == GetValNextUseInfo(ssaVal).GetNextUseIndex());
            TestAssert(useInfo.IsAllFutureUseGhostUse() == GetValNextUseInfo(ssaVal).IsAllFutureUseGhostUse());
            TestAssert(m_regValues[operandRegIdx] == ssaVal);
        }
        else
        {
            numRegsToEvict++;
            needLoad = true;
        }

        m_isSorted = false;

        // Unfortunately we can't use EvictUntil since it's possible that the operand gets evicted in edge case
        // if the node has reg alloc disabled because there are too many operands to fit in the reg,
        // so we must use our own implementation to save the operand from being evicted.
        //
        TestAssert(numRegsToEvict <= x_numRegs);

        if (numRegsToEvict > m_numScratchRegs)
        {
            size_t evictDataIdxBegin = x_numRegs - numRegsToEvict;
            size_t evictDataIdxEnd = x_numRegs - m_numScratchRegs;
            if (evictDataIdxBegin <= operandDataIdx && operandDataIdx < evictDataIdxEnd)
            {
                TestAssert(evictDataIdxBegin > 0);
                evictDataIdxBegin--;
            }
            for (size_t dataIdx = evictDataIdxBegin; dataIdx < evictDataIdxEnd; dataIdx++)
            {
                if (dataIdx == operandDataIdx)
                {
                    continue;
                }
                TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                uint8_t regIdx = ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8();
                TestAssert(regIdx < x_numRegs);
                EvictRegisterImpl(regIdx, false /*dueToDeath*/, false /*dueToTakenByOutput*/);
            }
        }

        TestAssert(m_numScratchRegs >= numRegsToEvict);

        if (needLoad)
        {
            UpdateValNextUseInfo(ssaVal, use.GetNextUseInfo());
            operandRegIdx = LoadValueToRegister(ssaVal, false /*isBornForOutput*/);
        }
        else
        {
            TestAssert(IsValAvailableInReg(ssaVal));
            TestAssert(operandRegIdx == GetValRegOrdInList(ssaVal));
            UpdateValNextUseInfo(ssaVal, use.GetNextUseInfo());
            UpdateNextUseInfo(operandRegIdx, use);
        }

        TestAssert(m_numScratchRegs >= requiredNumScratchRegs);

        if (shouldAlsoRelocateAllGroup1Gprs)
        {
            RelocateToTurnAllGroup1GprIntoScratch();
            // The operand may also be relocated (note that the 'needLoad' branch above will not load the operand into a
            // Group1 GPR if it was on the stack, since we always use the free register with the smallest ordinal in the
            // register list and Group1 GPRs comes last in the list; but if the operand is already in a Group1 GPR before
            // this function (the 'not needLoad' case), it will be relocated), so we need to update where the operand
            // is after relocation
            //
            TestAssert(IsValAvailableInReg(ssaVal));
            operandRegIdx = GetValRegOrdInList(ssaVal);
        }

        TestAssert(m_numScratchRegs >= requiredNumScratchRegs);
        TestAssert(operandRegIdx < x_numRegs);
        TestAssert(m_regValues[operandRegIdx] == ssaVal);
        TestAssert(GetValRegOrdInList(ssaVal) == operandRegIdx);
        AssertConsistency();

        RegAllocTypecheckCodegenDecisions res;
        res.m_inputRegIdx = operandRegIdx;
        TestAssert((m_scratchRegMask & (1U << operandRegIdx)) == 0);
        if (IsForGprState())
        {
            TestAssert(operandRegIdx < x_dfg_reg_alloc_num_gprs);
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_gprs) - 1) ^ m_scratchRegMask ^ (1U << operandRegIdx));
            res.m_nonOperandRegInfo.InitForGprState(m_numScratchRegs, m_scratchRegMask, passthruMask, requiredNumScratchRegs);
        }
        else
        {
            TestAssert(operandRegIdx < x_dfg_reg_alloc_num_fprs);
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_fprs) - 1) ^ m_scratchRegMask ^ (1U << operandRegIdx));
            res.m_nonOperandRegInfo.InitForFprState(m_numScratchRegs, m_scratchRegMask, passthruMask, requiredNumScratchRegs);
        }
        return res;
    }

    // Do register management to set up for codegen of the main node logic, update all uses
    //
    // After calling this function, all operands will be assigned a register
    //
    RegAllocMainCodegenDecisions WARN_UNUSED WorkForCodegen(RegAllocMainCodegenDesc& desc)
    {
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);

        SortRegisters();

        size_t numOperands = desc.m_numOperands;

        TestAssert(numOperands + (desc.m_hasOutput ? 1U : 0) + (desc.m_hasBrDecisionOutput ? 1U : 0) + desc.m_numScratchRegistersRequired <= x_numRegs);
        TestAssert(numOperands >= (desc.m_hasOutput && desc.m_outputMayReuseInputReg ? 1U : 0) + (desc.m_hasBrDecisionOutput && desc.m_brDecisionMayReuseInputReg ? 1U : 0));

        AssertMinimumUseIndex(desc.m_useIndex);

        bool shouldRelocateAllGroup1Gprs = IsForGprState() && desc.m_shouldTurnAllGprGroup1RegsToScratch;

        size_t numScratchRegsNeededByNode = desc.m_numScratchRegistersRequired;

        TestAssertImp(shouldRelocateAllGroup1Gprs, numScratchRegsNeededByNode >= x_dfg_reg_alloc_num_group1_gprs);

        size_t numRegsToEvict = numScratchRegsNeededByNode;
        uint32_t dataIdxMaskForOperandsInitiallyInReg = 0;
        size_t numInRegOperands = 0;

        static constexpr size_t x_regFieldShiftBits = 4;
        static_assert(ValueNextUseInfo::Data_CustomDataAsU16::BitWidth() >= x_regFieldShiftBits * 2);
        static_assert((1U << x_regFieldShiftBits) > x_numRegs);
        // Bits [4:8) of the CustomData field is the register ordinal
        // Bits [0:4) of the CustomData field is the ordinal of the operand in m_operands
        //
        // Note that the register ordinal is only there for sorting: they may be inaccurate at the end since we may move
        // registers around if 'shouldRelocateAllGroup1Gprs' is true! The source of truth is always in the ssaValue.
        //
        uint32_t nextUseForAllInRegOperands[x_numRegs];

        uint32_t operandIdxMaskForAllDuplicateEdges = 0;
        uint32_t operandIdxMaskForNotInRegOperands = 0;
        size_t numDuplicateEdges = 0;
        for (size_t i = 0; i < numOperands; i++)
        {
            ValueUseRAInfo op = desc.m_operands[i];
            ValueRegAllocInfo* ssaVal = op.GetSSAValue();
            if (op.IsDuplicateEdge())
            {
                // If this is a duplicate edge, we must evict one more reg since this operand will consume a scratch reg
                //
                numRegsToEvict++;
                numDuplicateEdges++;
                operandIdxMaskForAllDuplicateEdges |= (1U << i);
            }
            else
            {
                TestAssert(GetValNextUseInfo(ssaVal).GetNextUseIndex() == desc.m_useIndex);
                // If the value is not available in reg, we must evict one more reg since we need to load this operand
                //
                if (!IsValAvailableInReg(ssaVal))
                {
                    operandIdxMaskForNotInRegOperands |= (1U << i);
                    numRegsToEvict++;
                }
                else
                {
                    // For each operand available in reg, record and update its NextUse
                    //
                    ValueNextUseInfo nextUse = op.GetNextUseInfo();
                    TestAssert(nextUse.GetCustomDataAsU16() == 0);
                    TestAssert(numInRegOperands < x_numRegs);
                    uint8_t regIdx = GetValRegOrdInList(ssaVal);

                    nextUseForAllInRegOperands[numInRegOperands] = nextUse.m_value + static_cast<uint32_t>(i) + (static_cast<uint32_t>(regIdx) << x_regFieldShiftBits);
                    numInRegOperands++;

                    uint8_t dataIdx = m_regIdxToDataIdx[regIdx];
                    TestAssert(dataIdx < x_numRegs);
                    TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetNextUseIndex() == desc.m_useIndex);
                    TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                    TestAssert((dataIdxMaskForOperandsInitiallyInReg & (1U << dataIdx)) == 0);
                    dataIdxMaskForOperandsInitiallyInReg |= (1U << dataIdx);

                    UpdateValNextUseInfo(ssaVal, nextUse);
                    UpdateNextUseInfo(regIdx, op);
                }
            }
        }

        m_isSorted = false;

        // Note that we do not assume the operands are the only use edges with the current useIndex
        // since this can be overly strict for the caller logic. So we cannot assume that the operands already
        // in reg are exactly the first X items after sorting
        //
        TestAssert(dataIdxMaskForOperandsInitiallyInReg < (1U << (x_numRegs - m_numScratchRegs)));
#ifdef TESTBUILD
        for (size_t idx = 0; idx < x_numRegs; idx++)
        {
            TestAssertIff((m_data[idx] & x_isScratchMask) == 0, idx < x_numRegs - m_numScratchRegs);
        }
#endif

        // The dataIdx mask for all non-scratch register that is not storing an operand
        //
        uint32_t dataIdxMaskForNonOperandRegs = ((1U << (x_numRegs - m_numScratchRegs)) - 1) ^ dataIdxMaskForOperandsInitiallyInReg;

        // Evict registers
        //
        TestAssert(numRegsToEvict + numInRegOperands <= x_numRegs);

        if (numRegsToEvict > m_numScratchRegs)
        {
            // We must evict 'evictionCount' registers from 'dataIdxMaskForNonOperandRegs'
            //
            size_t evictionCount = numRegsToEvict - m_numScratchRegs;
            TestAssert(evictionCount <= CountNumberOfOnes(dataIdxMaskForNonOperandRegs));

            for (size_t i = 0; i < evictionCount; i++)
            {
                TestAssert(dataIdxMaskForNonOperandRegs != 0);
                size_t dataIdx = FindHighestOneBit(dataIdxMaskForNonOperandRegs);
                TestAssert(dataIdxMaskForNonOperandRegs & (1U << dataIdx));
                dataIdxMaskForNonOperandRegs ^= (1U << dataIdx);
                TestAssert(dataIdx < x_numRegs);
                TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                uint8_t regIdx = ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8();
                TestAssert(regIdx < x_numRegs);
                EvictRegisterImpl(regIdx, false /*dueToDeath*/, false /*dueToTakenByOutput*/);
            }
        }

        TestAssert(m_numScratchRegs >= numRegsToEvict);
#ifdef TESTBUILD
        for (size_t i = 0; i < x_numRegs; i++)
        {
            TestAssertImp((dataIdxMaskForNonOperandRegs & (1U << i)) > 0, (m_data[i] & x_isScratchMask) == 0);
        }
#endif

        // Load operands into registers
        // Load all the non-duplicate-edge operands first, and also update NextUse
        //
        if (operandIdxMaskForNotInRegOperands > 0)
        {
            for (size_t i = 0; i < numOperands; i++)
            {
                if (operandIdxMaskForNotInRegOperands & (1U << i))
                {
                    ValueUseRAInfo op = desc.m_operands[i];
                    ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                    TestAssert(!IsValAvailableInReg(ssaVal));
                    // Important to update next use before loading into register
                    //
                    TestAssert(GetValNextUseInfo(ssaVal).GetNextUseIndex() == desc.m_useIndex);
                    ValueNextUseInfo nextUse = op.GetNextUseInfo();
                    TestAssert(nextUse.GetCustomDataAsU16() == 0);
                    UpdateValNextUseInfo(ssaVal, nextUse);
                    // Load into register
                    //
                    uint8_t regIdx = LoadValueToRegister(ssaVal, false /*isBornForOutput*/);
                    // Since we always load into the free register with smallest ordinal in list,
                    // if 'shouldRelocateAllGroup1Gprs' is true, we should never be loading into Group 1 GPRs which come at the end of the list
                    //
                    TestAssertImp(shouldRelocateAllGroup1Gprs, regIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                    // Append to the list that tracks the NextUse of each in reg operand
                    //
                    TestAssert(numInRegOperands < x_numRegs);
                    nextUseForAllInRegOperands[numInRegOperands] = nextUse.m_value + static_cast<uint32_t>(i) + (static_cast<uint32_t>(regIdx) << x_regFieldShiftBits);
                    numInRegOperands++;
                }
            }
        }

        struct DuplicateEdgeRegInfo
        {
            uint8_t m_regIdx;
            uint8_t m_operandOrd;
        };

        // Assign register for the duplicate edges
        // Note that these are immediately invalidated after the node,
        // so we record them in m_reservedRegMask and make them back to scratch at the end of this function
        //
        DuplicateEdgeRegInfo duplicateEdgeRegs[x_numRegs];
        if (numDuplicateEdges > 0)
        {
            size_t curOrd = 0;
            TestAssert(m_numScratchRegs >= numDuplicateEdges);
            for (uint8_t i = 0; i < numOperands; i++)
            {
                if (operandIdxMaskForAllDuplicateEdges & (1U << i))
                {
                    TestAssert(m_numScratchRegs > 0 && m_scratchRegMask > 0);
                    m_numScratchRegs--;
                    uint8_t regIdx = static_cast<uint8_t>(CountTrailingZeros(m_scratchRegMask));
                    m_scratchRegMask ^= static_cast<uint16_t>(1U << regIdx);
                    TestAssert((m_reservedRegMask & static_cast<uint16_t>(1U << regIdx)) == 0);
                    m_reservedRegMask |= static_cast<uint16_t>(1U << regIdx);
                    TestAssertImp(shouldRelocateAllGroup1Gprs, regIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
                    ValueUseRAInfo op = desc.m_operands[i];
                    ValueRegAllocInfo* ssaVal = op.GetSSAValue();
                    TestAssert(op.IsDuplicateEdge());
                    TestAssert(IsValAvailableInReg(ssaVal));
                    uint8_t primaryOperandRegIdx = GetValRegOrdInList(ssaVal);
                    TriggerDuplicationEvent(primaryOperandRegIdx, regIdx);
                    duplicateEdgeRegs[curOrd] = { .m_regIdx = regIdx, .m_operandOrd = i };
                    curOrd++;
                }
            }
            TestAssert(curOrd == numDuplicateEdges);
        }

        TestAssert(m_numScratchRegs >= numScratchRegsNeededByNode);
        TestAssert(numInRegOperands + numDuplicateEdges == numOperands);

        struct OutputRegDecision
        {
            OutputRegDecision()
                : m_decidedToReuse(false)
                , m_reuseInputOrd(255)
                , m_regIdx(255)
            { }

            bool m_decidedToReuse;
            // m_operands[m_reuseInputOrd] is the input reg being taken
            // Note that if this is the case, the input reg is *not* spilled to the stack yet
            //
            uint8_t m_reuseInputOrd;
            // If m_decidedToReuse is false, the reg index of the output operand
            //
            // Note that if the output reused an input reg, the regIdx needs to be updated later, not now, since the input reg
            // may be a Group1 reg and was subsequentially relocated if shouldRelocateAllGroup1Gprs is true.
            // So this field is only valid if m_decidedToReuse is false
            //
            uint8_t m_regIdx;
        };

        size_t curEligibleDuplicateEdgeIdxForReuse = 0;

        auto decideOutputOperandRegister = [&](bool outputMayReuseInputReg,
                                               bool outputPrefersReuseInputReg) ALWAYS_INLINE WARN_UNUSED -> OutputRegDecision
        {
            OutputRegDecision r;
            if (outputMayReuseInputReg)
            {
                // If reusing is preferred, we should reuse if any of our operand reg will be spilled
                // before its next use, even if there are still free regs available
                //
                if (outputPrefersReuseInputReg)
                {
                    TestAssert(numDuplicateEdges >= curEligibleDuplicateEdgeIdxForReuse);
                    if (numDuplicateEdges > curEligibleDuplicateEdgeIdxForReuse)
                    {
                        // There is a duplicate edge, so that register is the best option for reuse
                        // since it doesn't need to be spilled at all
                        //
                        r.m_decidedToReuse = true;
                        r.m_reuseInputOrd = duplicateEdgeRegs[curEligibleDuplicateEdgeIdxForReuse].m_operandOrd;
                        curEligibleDuplicateEdgeIdxForReuse++;
                        return r;
                    }
                    else
                    {
                        // Check if any of the operand will be spilled before next use
                        //
                        TestAssert(numInRegOperands > 0);
                        uint8_t targetIdx = 255;
                        uint32_t largestNextUse = 0;
                        for (uint8_t i = 0; i < numInRegOperands; i++)
                        {
                            ValueNextUseInfo nextUse(nextUseForAllInRegOperands[i]);
                            if (nextUse.GetNextUseIndex() >= desc.m_nextSpillEverythingIndex)
                            {
                                if (nextUse.m_value >= largestNextUse)
                                {
                                    largestNextUse = nextUse.m_value;
                                    targetIdx = i;
                                }
                            }
                        }
                        if (targetIdx != 255)
                        {
                            r.m_decidedToReuse = true;
                            uint16_t payload = ValueNextUseInfo(nextUseForAllInRegOperands[targetIdx]).GetCustomDataAsU16();
                            uint8_t operandIdx = static_cast<uint8_t>(payload & ((1U << x_regFieldShiftBits) - 1));
                            r.m_reuseInputOrd = operandIdx;
                            nextUseForAllInRegOperands[targetIdx] = nextUseForAllInRegOperands[numInRegOperands - 1];
                            numInRegOperands--;
                            return r;
                        }
                    }
                }

                // If reusing is not preferred, or if we failed to find a reuse in the check above,
                // we should still try to reuse if we will have to spill a reg otherwise,
                // and spilling that reg is worse than spilling an operand reg
                //
                TestAssert(m_numScratchRegs >= numScratchRegsNeededByNode);
                if (m_numScratchRegs == numScratchRegsNeededByNode)
                {
                    // We should consider reuse since we will otherwise spill a register
                    // If we have duplicate edge, we can just reuse that register
                    //
                    TestAssert(numDuplicateEdges >= curEligibleDuplicateEdgeIdxForReuse);
                    if (numDuplicateEdges > curEligibleDuplicateEdgeIdxForReuse)
                    {
                        // There is a duplicate edge, that register is the best option for reuse
                        // since it doesn't need to be spilled at all
                        //
                        r.m_decidedToReuse = true;
                        r.m_reuseInputOrd = duplicateEdgeRegs[curEligibleDuplicateEdgeIdxForReuse].m_operandOrd;
                        curEligibleDuplicateEdgeIdxForReuse++;
                        return r;
                    }
                    else
                    {
                        TestAssert(dataIdxMaskForNonOperandRegs > 0);
                        size_t dataIdx = FindHighestOneBit(dataIdxMaskForNonOperandRegs);

                        // Figure out the largest next use of the operand regs
                        //
                        TestAssert(numInRegOperands > 0);
                        uint8_t targetIdx = 255;
                        uint32_t largestOperandNextUse = 0;
                        for (uint8_t i = 0; i < numInRegOperands; i++)
                        {
                            if (nextUseForAllInRegOperands[i] >= largestOperandNextUse)
                            {
                                largestOperandNextUse = nextUseForAllInRegOperands[i];
                                targetIdx = i;
                            }
                        }

                        if (m_data[dataIdx] < largestOperandNextUse)
                        {
                            // We should reuse the input operand
                            //
                            r.m_decidedToReuse = true;
                            uint16_t payload = ValueNextUseInfo(nextUseForAllInRegOperands[targetIdx]).GetCustomDataAsU16();
                            uint8_t operandIdx = static_cast<uint8_t>(payload & ((1U << x_regFieldShiftBits) - 1));
                            r.m_reuseInputOrd = operandIdx;
                            nextUseForAllInRegOperands[targetIdx] = nextUseForAllInRegOperands[numInRegOperands - 1];
                            numInRegOperands--;
                            return r;
                        }
                    }
                }
            }

            // We have decided to not reuse an input reg
            //
            TestAssert(m_numScratchRegs >= numScratchRegsNeededByNode);
            if (m_numScratchRegs == numScratchRegsNeededByNode)
            {
                // We don't have a spare scratch reg, evict another reg to hold the output
                //
                TestAssert(dataIdxMaskForNonOperandRegs > 0);
                size_t dataIdx = FindHighestOneBit(dataIdxMaskForNonOperandRegs);
                TestAssert(dataIdx < x_numRegs);
                TestAssert(dataIdxMaskForNonOperandRegs & (1U << dataIdx));
                dataIdxMaskForNonOperandRegs ^= (1U << dataIdx);
                TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                uint8_t regIdx = ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8();
                TestAssert(regIdx < x_numRegs);
                EvictRegisterImpl(regIdx, false /*dueToDeath*/, false /*dueToTakenByOutput*/);
            }

            // Ugly: we cannot direct load the value into the register, since there's a caller logic that may change this decision later.
            // So we only reserve this scratch register (by marking it in m_reservedRegMask) here,
            // which is sufficient to prevent this reg from being used by someone else
            //
            TestAssert(m_numScratchRegs > 0 && m_scratchRegMask > 0);
            m_numScratchRegs--;
            uint8_t regIdx = static_cast<uint8_t>(CountTrailingZeros(m_scratchRegMask));
            TestAssert(regIdx < x_numRegs);
            TestAssertImp(shouldRelocateAllGroup1Gprs, regIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
            m_scratchRegMask ^= static_cast<uint16_t>(1U << regIdx);
            TestAssert((m_reservedRegMask & static_cast<uint16_t>(1U << regIdx)) == 0);
            m_reservedRegMask |= static_cast<uint16_t>(1U << regIdx);
            r.m_decidedToReuse = false;
            r.m_regIdx = regIdx;
            TestAssert(m_numScratchRegs >= numScratchRegsNeededByNode);
            return r;
        };

        // Figure out the output/brDecision registers, including if we should let them take the position of an input operand
        //
        OutputRegDecision outputInfo;
        if (desc.m_hasOutput)
        {
            outputInfo = decideOutputOperandRegister(desc.m_outputMayReuseInputReg, desc.m_outputPrefersReuseInputReg);
        }

        OutputRegDecision brDecisionInfo;
        if (desc.m_hasBrDecisionOutput)
        {
            brDecisionInfo = decideOutputOperandRegister(desc.m_brDecisionMayReuseInputReg, desc.m_brDecisionPrefersReuseInputReg);
        }

        // Our backend does not support "output reuses, but brDecision does not reuse" combination.
        // So if that is the case, swap the output and brDecision register to satisfy the constraint
        //
        if (desc.m_hasOutput && desc.m_hasBrDecisionOutput)
        {
            TestAssertImp(desc.m_brDecisionMayReuseInputReg, desc.m_outputMayReuseInputReg);
            if (!outputInfo.m_decidedToReuse && brDecisionInfo.m_decidedToReuse)
            {
                std::swap(outputInfo, brDecisionInfo);
            }
        }

        // Sanity check the register decision is valid
        //
#ifdef TESTBUILD
        auto sanityCheckOutputRegDecision = [&](bool isOutput, OutputRegDecision& decision)
        {
            if (decision.m_decidedToReuse)
            {
                if (isOutput)
                {
                    TestAssert(desc.m_outputMayReuseInputReg);
                }
                else
                {
                    TestAssert(desc.m_brDecisionMayReuseInputReg);
                }
                TestAssert(decision.m_reuseInputOrd < numOperands);
            }
            else
            {
                TestAssert(decision.m_regIdx < x_numRegs);
                TestAssert(m_reservedRegMask & (1U << decision.m_regIdx));
                TestAssert((m_data[m_regIdxToDataIdx[decision.m_regIdx]] & x_isScratchMask) > 0);
            }
        };

        if (desc.m_hasOutput) { sanityCheckOutputRegDecision(true /*isOutput*/, outputInfo); }
        if (desc.m_hasBrDecisionOutput) { sanityCheckOutputRegDecision(false /*isOutput*/, brDecisionInfo); }

        if (desc.m_hasOutput && desc.m_hasBrDecisionOutput)
        {
            TestAssert(!(!outputInfo.m_decidedToReuse && brDecisionInfo.m_decidedToReuse));
            if (outputInfo.m_decidedToReuse && brDecisionInfo.m_decidedToReuse)
            {
                TestAssert(outputInfo.m_reuseInputOrd != brDecisionInfo.m_reuseInputOrd);
            }
            if (!outputInfo.m_decidedToReuse && !brDecisionInfo.m_decidedToReuse)
            {
                TestAssert(outputInfo.m_regIdx != brDecisionInfo.m_regIdx);
            }
        }
#endif

        if (shouldRelocateAllGroup1Gprs)
        {
            RelocateToTurnAllGroup1GprIntoScratch();
        }

        // Now we know the final position of all the input registers
        // Set up the decided register for each input operand
        //
        RegAllocMainCodegenDecisions r;
        r.m_numInputRegsInThisBank = SafeIntegerCast<uint8_t>(numOperands);

        // The mask of regIdx that gets used by an input operand or an output
        //
        uint16_t regIdxMaskUsedByOperandOrOutput = 0;

        for (size_t i = 0; i < numOperands; i++)
        {
            r.m_inputRegIdx[i] = 255;
            if ((operandIdxMaskForAllDuplicateEdges & (1U << i)) == 0)
            {
                TestAssert(!desc.m_operands[i].IsDuplicateEdge());
                ValueRegAllocInfo* ssaVal = desc.m_operands[i].GetSSAValue();
                TestAssert(IsValAvailableInReg(ssaVal));
                uint8_t regIdx = GetValRegOrdInList(ssaVal);
                r.m_inputRegIdx[i] = regIdx;
                TestAssert((regIdxMaskUsedByOperandOrOutput & (1U << regIdx)) == 0);
                regIdxMaskUsedByOperandOrOutput |= static_cast<uint16_t>(1U << regIdx);
            }
            else
            {
                TestAssert(desc.m_operands[i].IsDuplicateEdge());
            }
        }

        for (size_t i = 0; i < numDuplicateEdges; i++)
        {
            uint8_t regIdx = duplicateEdgeRegs[i].m_regIdx;
            TestAssert(regIdx < x_numRegs);
            uint8_t operandIdx = duplicateEdgeRegs[i].m_operandOrd;
            TestAssert(operandIdx < numOperands);
            TestAssert(r.m_inputRegIdx[operandIdx] == 255);
            r.m_inputRegIdx[operandIdx] = regIdx;
            TestAssert((regIdxMaskUsedByOperandOrOutput & (1U << regIdx)) == 0);
            regIdxMaskUsedByOperandOrOutput |= static_cast<uint16_t>(1U << regIdx);
        }

        // Sanity check that our register assignment makes sense
        //
#ifdef TESTBUILD
        for (size_t i = 0; i < numOperands; i++) { TestAssert(r.m_inputRegIdx[i] < x_numRegs); }
        for (size_t i = 0; i < numOperands; i++)
        {
            for (size_t j = i + 1; j < numOperands; j++)
            {
                TestAssert(r.m_inputRegIdx[i] != r.m_inputRegIdx[j]);
            }
        }
        if (shouldRelocateAllGroup1Gprs)
        {
            for (size_t i = 0; i < numOperands; i++)
            {
                TestAssert(r.m_inputRegIdx[i] < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
            }
        }
#endif

        m_outputRegMask = 0;

        // If the output/brDecision is reusing one of the input registers, we should spill the input reg and set up the output reg.
        // If the output/brDecision has their own register, we should set up the output reg
        //
        auto finalizeOutputRegister = [&](ValueRegAllocInfo* val, OutputRegDecision& info /*inout*/) ALWAYS_INLINE
        {
            if (!info.m_decidedToReuse)
            {
                TestAssert(info.m_regIdx < x_numRegs);
                TestAssert((m_reservedRegMask & (1U << info.m_regIdx)) > 0);
                TestAssert((m_scratchRegMask & (1U << info.m_regIdx)) == 0);
                m_reservedRegMask ^= static_cast<uint16_t>(1U << info.m_regIdx);
                m_scratchRegMask |= static_cast<uint16_t>(1U << info.m_regIdx);
                m_numScratchRegs++;

                TestAssert((regIdxMaskUsedByOperandOrOutput & (1U << info.m_regIdx)) == 0);
                regIdxMaskUsedByOperandOrOutput |= static_cast<uint16_t>(1U << info.m_regIdx);
            }
            else
            {
                TestAssert(info.m_regIdx == 255);
                TestAssert(info.m_reuseInputOrd < x_numRegs);
                TestAssert(info.m_reuseInputOrd < numOperands);
                uint8_t regIdx = r.m_inputRegIdx[info.m_reuseInputOrd];
                if ((operandIdxMaskForAllDuplicateEdges & (1U << info.m_reuseInputOrd)) > 0)
                {
                    // This is a duplicate edge, it's not holding an SSA value so don't need to (and cannot) spill it
                    // But we need to make it a scratch register so we can load into it
                    //
                    TestAssert((m_reservedRegMask & (1U << regIdx)) > 0);
                    TestAssert((m_scratchRegMask & (1U << regIdx)) == 0);
                    m_reservedRegMask ^= static_cast<uint16_t>(1U << regIdx);
                    m_scratchRegMask |= static_cast<uint16_t>(1U << regIdx);
                    m_numScratchRegs++;
                }
                else
                {
                    // We must spill this input register
                    //
                    EvictRegisterImpl(regIdx, false /*dueToDeath*/, true /*dueToTakenByOutput*/);
                }
                info.m_regIdx = regIdx;

                TestAssert((regIdxMaskUsedByOperandOrOutput & (1U << regIdx)) > 0);
            }
            // The output register is born
            //
            TestAssert(info.m_regIdx < x_numRegs);
            LoadValueToGivenRegister(val, info.m_regIdx, true /*isBornForOutput*/);
        };

        if (desc.m_hasOutput)
        {
            finalizeOutputRegister(desc.m_outputVal, outputInfo /*inout*/);
            r.m_outputReusedInputRegister = outputInfo.m_decidedToReuse;
            r.m_outputReusedInputOperandOrd = outputInfo.m_reuseInputOrd;
            r.m_outputRegIdx = outputInfo.m_regIdx;
        }
        else
        {
            r.m_outputReusedInputRegister = false;
            r.m_outputReusedInputOperandOrd = 255;
            r.m_outputRegIdx = 255;
        }

        if (desc.m_hasBrDecisionOutput)
        {
            finalizeOutputRegister(desc.m_brDecisionVal, brDecisionInfo /*inout*/);
            r.m_brDecisionReusedInputRegister = brDecisionInfo.m_decidedToReuse;
            r.m_brDecisionReusedInputOperandOrd = brDecisionInfo.m_reuseInputOrd;
            r.m_brDecisionRegIdx = brDecisionInfo.m_regIdx;
        }
        else
        {
            r.m_brDecisionReusedInputRegister = false;
            r.m_brDecisionReusedInputOperandOrd = 255;
            r.m_brDecisionRegIdx = 255;
        }

        // Sanity check that our output register assignment makes sense
        //
#ifdef TESTBUILD
        TestAssertImp(desc.m_hasOutput, r.m_outputRegIdx < x_numRegs);
        TestAssertImp(desc.m_hasBrDecisionOutput, r.m_brDecisionRegIdx < x_numRegs);
        if (desc.m_hasOutput && desc.m_hasBrDecisionOutput)
        {
            TestAssert(r.m_outputRegIdx != r.m_brDecisionRegIdx);
        }
        if (shouldRelocateAllGroup1Gprs)
        {
            TestAssertImp(desc.m_hasOutput, r.m_outputRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
            TestAssertImp(desc.m_hasBrDecisionOutput, r.m_brDecisionRegIdx < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
        }
        if (desc.m_hasOutput)
        {
            bool found = false;
            for (size_t i = 0; i < numOperands; i++)
            {
                if (r.m_inputRegIdx[i] == r.m_outputRegIdx)
                {
                    found = true;
                }
            }
            TestAssertIff(r.m_outputReusedInputRegister, found);
            TestAssertImp(r.m_outputReusedInputRegister, r.m_outputReusedInputOperandOrd < numOperands);
            TestAssertImp(r.m_outputReusedInputRegister, r.m_inputRegIdx[r.m_outputReusedInputOperandOrd] == r.m_outputRegIdx);
        }
        if (desc.m_hasBrDecisionOutput)
        {
            bool found = false;
            for (size_t i = 0; i < numOperands; i++)
            {
                if (r.m_inputRegIdx[i] == r.m_brDecisionRegIdx)
                {
                    found = true;
                }
            }
            TestAssertIff(r.m_brDecisionReusedInputRegister, found);
            TestAssertImp(r.m_brDecisionReusedInputRegister, r.m_brDecisionReusedInputOperandOrd < numOperands);
            TestAssertImp(r.m_brDecisionReusedInputRegister, r.m_inputRegIdx[r.m_brDecisionReusedInputOperandOrd] == r.m_brDecisionRegIdx);
        }
        for (size_t i = 0; i < numOperands; i++)
        {
            TestAssert((m_scratchRegMask & (1U << r.m_inputRegIdx[i])) == 0);
        }
        TestAssertImp(desc.m_hasOutput, (m_scratchRegMask & (1U << r.m_outputRegIdx)) == 0);
        TestAssertImp(desc.m_hasBrDecisionOutput, (m_scratchRegMask & (1U << r.m_brDecisionRegIdx)) == 0);
#endif

        TestAssert(m_numScratchRegs >= numScratchRegsNeededByNode);
        TestAssert(m_numScratchRegs == CountNumberOfOnes(m_scratchRegMask));

        AssertConsistency();

        TestAssert((regIdxMaskUsedByOperandOrOutput & m_scratchRegMask) == 0);

        // Populate the register information for scratch and passthru registers
        //
        if (IsForGprState())
        {
            TestAssert(regIdxMaskUsedByOperandOrOutput < (1U << x_dfg_reg_alloc_num_gprs));
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_gprs) - 1) ^ m_scratchRegMask ^ regIdxMaskUsedByOperandOrOutput);
            r.m_nonOperandRegInfo.InitForGprState(m_numScratchRegs, m_scratchRegMask, passthruMask, numScratchRegsNeededByNode);
        }
        else
        {
            TestAssert(regIdxMaskUsedByOperandOrOutput < (1U << x_dfg_reg_alloc_num_fprs));
            uint16_t passthruMask = static_cast<uint16_t>(((1U << x_dfg_reg_alloc_num_fprs) - 1) ^ m_scratchRegMask ^ regIdxMaskUsedByOperandOrOutput);
            r.m_nonOperandRegInfo.InitForFprState(m_numScratchRegs, m_scratchRegMask, passthruMask, numScratchRegsNeededByNode);
        }

        // All reserved regs automatically become scratch reg after node execution
        // (recall that after this function call, the state of this class is updated to what happens after node execution,
        // but the RegEventCallBacks are called to reflect the state right before node execuction, i.e., before the outputs are born)
        //
        TestAssert((m_scratchRegMask & m_reservedRegMask) == 0);
        m_scratchRegMask |= m_reservedRegMask;
        m_numScratchRegs = static_cast<uint8_t>(CountNumberOfOnes(m_scratchRegMask));
        m_reservedRegMask = 0;

        AssertConsistency();

        return r;
    }

    // 'func' should have prototype void(uint8_t regIdx, ValueRegAllocInfo* val)
    //
    template<typename Func>
    void ALWAYS_INLINE ForEachNonScratchRegister(const Func& func)
    {
        AssertConsistency();
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);
        uint16_t fullMask = static_cast<uint16_t>((1U << x_numRegs) - 1);
        TestAssert(m_scratchRegMask <= fullMask);
        uint16_t inUseRegMask = fullMask ^ m_scratchRegMask;
        while (inUseRegMask > 0)
        {
            uint8_t regIdx = static_cast<uint8_t>(CountTrailingZeros(inUseRegMask));
            inUseRegMask ^= static_cast<uint16_t>(1U << regIdx);
            TestAssert(m_regValues[regIdx] != nullptr);
            func(regIdx, m_regValues[regIdx]);
        }
    }

    // Spill all registers to stack, so all registers become scratch
    //
    void SpillEverything()
    {
        AssertConsistency();
        TestAssert(m_reservedRegMask == 0 && m_outputRegMask == 0);
        uint16_t fullMask = static_cast<uint16_t>((1U << x_numRegs) - 1);
        while (m_scratchRegMask < fullMask)
        {
            uint8_t regIdx = static_cast<uint8_t>(CountTrailingZeros(static_cast<uint16_t>(m_scratchRegMask ^ fullMask)));
            TestAssert(regIdx < x_numRegs);
            EvictRegisterImpl(regIdx, false /*dueToDeath*/, false /*dueToTakenByOutput*/);
        }
        TestAssert(m_scratchRegMask == fullMask);
        TestAssert(m_numScratchRegs == x_numRegs);
        AssertConsistency();
    }

    // Process the event that register 'regIdx' is dead because the value it is holding (ssaVal) is dead
    //
    void ProcessDeathEvent(uint8_t regIdx, [[maybe_unused]] ValueRegAllocInfo* ssaVal)
    {
        AssertConsistency();
        TestAssert(regIdx < x_numRegs);
        TestAssert(GetValRegOrdInList(ssaVal) == regIdx);
        TestAssert((m_scratchRegMask & (1U << regIdx)) == 0);
        TestAssert(ssaVal == m_regValues[regIdx]);
        TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        EvictRegisterImpl(regIdx, true /*dueToDeath*/, false /*dueToTakenByOutput*/);
        TestAssert((m_scratchRegMask & (1U << regIdx)) > 0);
        TestAssert(!IsValAvailableInReg(ssaVal));
    }

    // This function allows the caller to notify us that 'ssaVal' is born in 'regIdx' through some mechanism outside the management of this class
    // 'regIdx' must be a scratch right now.
    //
    void ProcessHackyBorn(uint8_t regIdx, ValueRegAllocInfo* ssaVal)
    {
        AssertConsistency();
        LoadValueToGivenRegister(ssaVal, regIdx, true /*isBornForOutput*/);
    }

    void ClearOutputRegMaskAfterBornEvents()
    {
        m_outputRegMask = 0;
        AssertConsistency();
    }

    void AssertConsistency()
    {
#ifdef TESTBUILD
        TestAssert(m_scratchRegMask < (1U << x_numRegs));
        TestAssert(m_numScratchRegs == CountNumberOfOnes(m_scratchRegMask));
        TestAssert((m_reservedRegMask & m_scratchRegMask) == 0);
        for (size_t i = 0; i < x_numRegs; i++)
        {
            TestAssertIff((m_scratchRegMask & (1U << i)) == 0 && (m_reservedRegMask & (1U << i)) == 0, m_regValues[i] != nullptr);
        }
        bool seen[x_numRegs];
        memset(seen, 0, sizeof(bool) * x_numRegs);
        for (size_t i = 0; i < x_numRegs; i++)
        {
            TestAssert(m_regIdxToDataIdx[i] < x_numRegs);
            TestAssert(!seen[m_regIdxToDataIdx[i]]);
            seen[m_regIdxToDataIdx[i]] = true;
            TestAssert(ValueNextUseInfo(m_data[m_regIdxToDataIdx[i]]).GetCustomDataAsU8() == i);
        }
        TestAssert((m_outputRegMask & m_scratchRegMask) == 0);
        for (size_t i = 0; i < x_numRegs; i++)
        {
            ValueNextUseInfo useInfo = ValueNextUseInfo(m_data[i]);
            TestAssertImp(useInfo.GetNextUseIndex() == x_noNextUse, useInfo.IsAllFutureUseGhostUse());
            TestAssertImp((m_data[i] & x_isScratchMask) > 0, useInfo.GetNextUseIndex() == x_noNextUse);
            uint8_t regIdx = useInfo.GetCustomDataAsU8();
            TestAssert(regIdx < x_numRegs);
            ValueRegAllocInfo* node = m_regValues[regIdx];
            TestAssertIff((m_data[i] & x_isScratchMask) == 0, node != nullptr);
            TestAssertImp((m_outputRegMask & (1U << regIdx)) > 0, node != nullptr);
            if (node != nullptr)
            {
                ValueNextUseInfo info = GetValNextUseInfo(node);
                TestAssert(info.IsAllFutureUseGhostUse() == useInfo.IsAllFutureUseGhostUse());
                TestAssert(info.GetNextUseIndex() == useInfo.GetNextUseIndex());
                if ((m_outputRegMask & (1U << regIdx)) > 0)
                {
                    // This node is not born yet so should not be in anywhere
                    //
                    TestAssert(!node->IsAvailableInGPR() && !node->IsAvailableInFPR());
                    TestAssert(!node->IsConstantLikeNode());
                    TestAssert(!node->IsSpilled());
                }
                else
                {
                    TestAssert(IsValAvailableInReg(node));
                    TestAssert(GetValRegOrdInList(node) == regIdx);
                }
            }
            // After sorting, all scratch regs must be at the end of the list since they have x_isScratchMask and all higher bits set
            //
            if (m_isSorted)
            {
                TestAssertIff(node == nullptr, i >= x_numRegs - m_numScratchRegs);
            }
        }
        if (m_isSorted)
        {
            for (size_t i = 1; i < x_numRegs; i++)
            {
                TestAssert(m_data[i - 1] < m_data[i]);
            }
        }
#endif
    }

    void AssertAllRegistersScratched()
    {
        TestAssert(m_numScratchRegs == x_numRegs);
    }

    // Whether this class is managing the GPR state or the FPR state
    //
    static constexpr bool IsForGprState() { return forGprState; }

private:
    void ALWAYS_INLINE TriggerEvictionEvent(ValueRegAllocInfo* ssaVal, size_t regIdx, bool dueToTakenByOutput)
    {
        TestAssert(regIdx < x_numRegs);
        TestAssert(ssaVal->GetRegBankRegOrdInList<IsForGprState()>() == regIdx);
        m_cbs.template EvictRegister<IsForGprState()>(ssaVal, regIdx, dueToTakenByOutput);
        TestAssert(!IsValAvailableInReg(ssaVal));
    }

    void ALWAYS_INLINE TriggerRelocationEvent(ValueRegAllocInfo* ssaVal, size_t fromRegIdx, size_t toRegIdx)
    {
        TestAssert(fromRegIdx < x_numRegs && toRegIdx < x_numRegs && fromRegIdx != toRegIdx);
        TestAssert(ssaVal->GetRegBankRegOrdInList<IsForGprState()>() == fromRegIdx);
        m_cbs.template RelocateRegister<IsForGprState()>(ssaVal, fromRegIdx, toRegIdx);
        TestAssert(GetValRegOrdInList(ssaVal) == toRegIdx);
    }

    void ALWAYS_INLINE TriggerDuplicationEvent(size_t fromRegIdx, size_t toRegIdx)
    {
        TestAssert(fromRegIdx < x_numRegs && toRegIdx < x_numRegs && fromRegIdx != toRegIdx);
        m_cbs.template DuplicateRegister<IsForGprState()>(fromRegIdx, toRegIdx);
    }

    void ALWAYS_INLINE TriggerLoadEvent(ValueRegAllocInfo* ssaVal, size_t regIdx)
    {
        TestAssert(regIdx < x_numRegs);
        TestAssert(!IsValAvailableInReg(ssaVal));
        m_cbs.template LoadRegister<IsForGprState()>(ssaVal, regIdx);
        TestAssert(GetValRegOrdInList(ssaVal) == regIdx);
    }

    void ALWAYS_INLINE TriggerKillEvent(ValueRegAllocInfo* ssaVal, size_t regIdx)
    {
        TestAssert(regIdx < x_numRegs);
        TestAssert(ssaVal->GetRegBankRegOrdInList<IsForGprState()>() == regIdx);
        m_cbs.template KillRegister<IsForGprState()>(ssaVal, regIdx);
        TestAssert(!IsValAvailableInReg(ssaVal));
    }

    bool ALWAYS_INLINE IsValAvailableInReg(ValueRegAllocInfo* ssaVal)
    {
        return ssaVal->IsAvailableInRegBank<IsForGprState()>();
    }

    uint8_t ALWAYS_INLINE GetValRegOrdInList(ValueRegAllocInfo* ssaVal)
    {
        TestAssert(IsValAvailableInReg(ssaVal));
        uint8_t res = ssaVal->GetRegBankRegOrdInList<IsForGprState()>();
        TestAssert(res < x_numRegs);
        TestAssert(m_regValues[res] == ssaVal);
        TestAssert(m_regIdxToDataIdx[res] < x_numRegs);
        TestAssert(ValueNextUseInfo(m_data[m_regIdxToDataIdx[res]]).GetCustomDataAsU8() == res);
        TestAssert((m_data[m_regIdxToDataIdx[res]] & x_isScratchMask) == 0);
        return res;
    }

    ValueNextUseInfo ALWAYS_INLINE GetValNextUseInfo(ValueRegAllocInfo* ssaVal)
    {
        return ssaVal->GetNextUseInfoInRegBank<IsForGprState()>();
    }

    void ALWAYS_INLINE UpdateValNextUseInfo(ValueRegAllocInfo* ssaVal, ValueNextUseInfo info)
    {
        ssaVal->SetNextUseInfoInRegBank<IsForGprState()>(info);
    }

    // Note that this function does not update m_isSorted. Caller must update it correspondingly.
    //
    void ALWAYS_INLINE EvictRegisterImpl(uint8_t regIdx, bool dueToDeath, bool dueToTakenByOutput)
    {
        TestAssert(regIdx < x_numRegs);
        ValueRegAllocInfo* info = m_regValues[regIdx];
        TestAssert(info != nullptr);
        TestAssert(GetValRegOrdInList(info) == regIdx);
        m_regValues[regIdx] = nullptr;
        TestAssert((m_scratchRegMask & (1U << regIdx)) == 0);
        TestAssert((m_reservedRegMask & (1U << regIdx)) == 0);
        uint8_t dataIdx = m_regIdxToDataIdx[regIdx];
        TestAssert(dataIdx < x_numRegs);
        // It is possible that m_data[dataIdx] is already NoMoreUse (i.e., no more use in this register bank),
        // but we need to trigger the eviction event in this case as well: we can't just silently discard the value
        // since it's possible that the value is still needed by the other register bank!
        //
        TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
        TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetNextUseIndex() == GetValNextUseInfo(info).GetNextUseIndex());
        TestAssert(ValueNextUseInfo(m_data[dataIdx]).IsAllFutureUseGhostUse() == GetValNextUseInfo(info).IsAllFutureUseGhostUse());
        TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8() == regIdx);
        TestAssertImp(dueToDeath, !ValueNextUseInfo(m_data[dataIdx]).HasNextUse());
        m_data[dataIdx] = ValueNextUseInfo::NoMoreUse().m_value | x_isScratchMask | regIdx;
        m_numScratchRegs++;
        m_scratchRegMask |= static_cast<uint16_t>(1U << regIdx);
        if (!dueToDeath)
        {
            TriggerEvictionEvent(info, regIdx, dueToTakenByOutput);
        }
        else
        {
            TestAssert(!dueToTakenByOutput);
            TriggerKillEvent(info, regIdx);
        }
    }

    void LoadValueToGivenRegister(ValueRegAllocInfo* val, uint8_t regIdx, bool isBornForOutput)
    {
        m_isSorted = false;
        TestAssert(!IsValAvailableInReg(val));
        TestAssert(m_numScratchRegs > 0);
        TestAssert(regIdx < x_numRegs);
        TestAssert((m_scratchRegMask & (1U << regIdx)) > 0);
        m_scratchRegMask ^= static_cast<uint16_t>(1U << regIdx);
        m_numScratchRegs--;
        TestAssert(m_regValues[regIdx] == nullptr);
        m_regValues[regIdx] = val;
        uint8_t dataIdx = m_regIdxToDataIdx[regIdx];
        TestAssert(dataIdx < x_numRegs);
        TestAssert((m_data[dataIdx] & x_isScratchMask) > 0);
        TestAssert(!ValueNextUseInfo(m_data[dataIdx]).HasNextUse());
        TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8() == regIdx);
        ValueNextUseInfo nextUse = GetValNextUseInfo(val);
        TestAssert(nextUse.GetCustomDataAsU16() == 0);
        m_data[dataIdx] = nextUse.m_value + regIdx;
        if (!isBornForOutput)
        {
            TriggerLoadEvent(val, regIdx);
        }
        else
        {
            TestAssert((m_outputRegMask & (1U << regIdx)) == 0);
            m_outputRegMask |= static_cast<uint16_t>(1U << regIdx);
        }
    }

    // Load value to the free register with the smallest ordinal in the reg list
    //
    uint8_t LoadValueToRegister(ValueRegAllocInfo* val, bool isBornForOutput)
    {
        TestAssert(m_numScratchRegs > 0 && m_scratchRegMask != 0);
        uint8_t regIdx = static_cast<uint8_t>(CountTrailingZeros(m_scratchRegMask));
        TestAssert(regIdx < x_numRegs);
        LoadValueToGivenRegister(val, regIdx, isBornForOutput);
        return regIdx;
    }

    void RelocateToTurnAllGroup1GprIntoScratch()
    {
        TestAssert(IsForGprState());
        TestAssert(m_numScratchRegs >= x_dfg_reg_alloc_num_group1_gprs);
        // All Group1 GPRs come after all Group2 GPRs
        //
        uint8_t firstGroup1GprIdx = static_cast<uint8_t>(x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
        for (uint8_t regIdx = firstGroup1GprIdx; regIdx < x_dfg_reg_alloc_num_gprs; regIdx++)
        {
            TestAssert((m_reservedRegMask & (1U << regIdx)) == 0);
            if (m_regValues[regIdx] != nullptr)
            {
                m_isSorted = false;

                TestAssert(m_scratchRegMask != 0);
                uint8_t dstRegIdx = static_cast<uint8_t>(CountTrailingZeros(m_scratchRegMask));
                TestAssert(dstRegIdx < x_numRegs);
                TestAssert(dstRegIdx < firstGroup1GprIdx);
                m_scratchRegMask ^= static_cast<uint16_t>(1U << dstRegIdx);
                TestAssert((m_scratchRegMask & (1U << regIdx)) == 0);
                m_scratchRegMask |= static_cast<uint16_t>(1U << regIdx);

                ValueRegAllocInfo* ssaVal = m_regValues[regIdx];

                uint8_t dataIdx = m_regIdxToDataIdx[regIdx];
                TestAssert(dataIdx < x_numRegs);
                uint8_t dstDataIdx = m_regIdxToDataIdx[dstRegIdx];
                TestAssert(dstDataIdx < x_numRegs);

                TestAssert((m_data[dstDataIdx] & x_isScratchMask) > 0);
                TestAssert(!ValueNextUseInfo(m_data[dstDataIdx]).HasNextUse());
                TestAssert(ValueNextUseInfo(m_data[dstDataIdx]).GetCustomDataAsU8() == dstRegIdx);

                TestAssert((m_data[dataIdx] & x_isScratchMask) == 0);
                TestAssert(ValueNextUseInfo(m_data[dataIdx]).GetCustomDataAsU8() == regIdx);

                m_data[dstDataIdx] = ValueNextUseInfo(m_data[dataIdx]).GetCompositeValueWithLowerBitsCleared() | dstRegIdx;
                m_data[dataIdx] = ValueNextUseInfo::NoMoreUse().m_value | x_isScratchMask | regIdx;

                m_regValues[dstRegIdx] = ssaVal;
                m_regValues[regIdx] = nullptr;

                TriggerRelocationEvent(ssaVal, regIdx, dstRegIdx);

                AssertConsistency();
            }
        }

#ifdef TESTBUILD
        for (uint8_t regIdx = firstGroup1GprIdx; regIdx < x_dfg_reg_alloc_num_gprs; regIdx++)
        {
            TestAssert(m_regValues[regIdx] == nullptr);
        }
        AssertConsistency();
#endif
    }

    static_assert(
        []() {
            for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs; i++)
            {
                if (i < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)
                {
                    ReleaseAssert(x_dfg_reg_alloc_gprs[i].MachineOrd() >= 8 && "Group 2 GPR registers (r8-r15) must come before all Group 1 registers!");
                }
                else
                {
                    ReleaseAssert(x_dfg_reg_alloc_gprs[i].MachineOrd() < 8 && "Group 2 GPR registers (r8-r15) must come before all Group 1 registers!");
                }
            }
            return true;
        }());

    RegEventCallbacks& m_cbs;

    static constexpr size_t x_numRegs = IsForGprState() ? x_dfg_reg_alloc_num_gprs : x_dfg_reg_alloc_num_fprs;

    static constexpr uint32_t x_isScratchMask = (1U << (ValueNextUseInfo::Data_CustomDataAsU16::BitWidth() - 1));
    static constexpr uint32_t x_noNextUse = ValueNextUseInfo::x_noNextUse;

    // The eviction priority data. This is sorted when StartNewDfgNode() is called
    // Bits [31:12] is the ValueNextUseInfo
    // We use bit 11 to denote if this is a scratch register: 1 implies bits [31:12] are also NoNextUse (all 1),
    // and the eviction event has been triggered so m_regValues[regIdx] is nullptr; 0 implies m_regValues[regIdx] is not nullptr.
    // The lower bits are used to store the regIdx
    //
    uint32_t m_data[x_numRegs];

    // m_regIdxToDataIdx[i] is the index into m_data where register index i is stored
    //
    uint8_t m_regIdxToDataIdx[x_numRegs];

    // Whether we know m_data is already in sorted state
    //
    bool m_isSorted;

    // The total number of scratch regs available right now
    //
    uint8_t m_numScratchRegs;

    // The mask for which registers are scratch registers
    //
    uint16_t m_scratchRegMask;

    // The mask for which registers have been reserved for use but not associated with an SSA value
    // This is only used during WorkForCodegen(), so it is always 0 except during WorkForCodegen()
    //
    // Specifically, the invariant is that if a reg is in m_reservedRegMask, the corresponding m_regValue should be nullptr,
    // and the corresponding m_data should be scratch (scratch bit set, next use is NoUse).
    //
    // The primary use case of this mask is to reserve registers for duplication edges: we must assign a different
    // register for each duplicate edge since that's what's being assumed by the underlying JIT code.
    // These registers automatically become scratch after executing a node, so at end of WorkForCodegen(),
    // we will turn these registers into scratch.
    //
    uint16_t m_reservedRegMask;

    // This mask indicates the outputs that haven't been born in ValueManager, for assertion purpose only
    //
    uint16_t m_outputRegMask;

    // m_regValues[i] is the ValueRegAllocInfo currently stored in register index i,
    // nullptr if it is not holding an SSA value
    //
    ValueRegAllocInfo* m_regValues[x_numRegs];

    // Records for handling duplicate edges in the range operands
    //
    TempVector<std::pair<uint16_t /*ord*/, uint16_t /*next*/>> m_rangedOperandDuplicateEdgeList;
    TempUnorderedMap<uint32_t /*ValueRegAllocInfoPtr*/, uint16_t /*head*/> m_rangedOperandDuplicateEdgeMap;
    TempVector<uint32_t /*ValueRegAllocInfoPtr*/> m_rangedOperandOrdToHandle;
    TempVector<uint32_t> m_rangedOperandInfoList;
    TempVector<uint16_t> m_rangedOperandResultOrder;
};

}   // namespace dfg
