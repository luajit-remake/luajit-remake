#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_node_info.h"
#include "dfg_osr_exit_map_builder.h"
#include "dfg_reg_alloc_codegen_state.h"
#include "dfg_virtual_register.h"
#include "dfg_logical_variable_info.h"
#include "dfg_codegen_operation_log.h"

namespace dfg {

// This class tracks the location of each SSA value and the current register/stack spill state,
// and provides API for all register operations
//
struct RegAllocValueManager
{
    MAKE_NONCOPYABLE(RegAllocValueManager);
    MAKE_NONMOVABLE(RegAllocValueManager);

    RegAllocValueManager(TempArenaAllocator& alloc,
                         uint16_t numTotalShadowStackSlots,
                         uint16_t firstRegSpillPhysicalSlot,
                         uint16_t numPhysicalSlotsForLocals)
        : m_osrExitMap(alloc, numTotalShadowStackSlots)
        , m_codegenLog(alloc, firstRegSpillPhysicalSlot)
        , m_firstGprRegisterPhysicalSlot(firstRegSpillPhysicalSlot)
        , m_firstFprRegisterPhysicalSlot(static_cast<uint16_t>(m_firstGprRegisterPhysicalSlot + x_dfg_reg_alloc_num_gprs))
        , m_firstLocalPhysicalSlot(static_cast<uint16_t>(m_firstFprRegisterPhysicalSlot + x_dfg_reg_alloc_num_fprs))
        , m_firstStackSpillPhysicalSlot(m_firstLocalPhysicalSlot + numPhysicalSlotsForLocals)
        , m_totalNumPhysicalSlots(m_firstStackSpillPhysicalSlot)
        , m_historyMaxNumPhysicalSlots(m_totalNumPhysicalSlots)
        , m_spillSlotFreeList(alloc)
#ifdef TESTBUILD
        , m_valuesBorn(alloc)
        , m_valuesDead(alloc)
        , m_constantsMaterialized(alloc)
        , m_constantsDead(alloc)
        , m_inUseSpillSlotsMap(alloc)
        , m_freeSpillSlotsMap(alloc)
        , m_disallowAllocationOfNewSpillSlots(false)
#endif
    { }

    // Note that m_historyMaxNumPhysicalSlots is not reset since it records the history max across all basic blocks
    //
    void ResetForNewBasicBlock()
    {
        m_osrExitMap.Reset();
        m_codegenLog.ResetLog();
        m_totalNumPhysicalSlots = m_firstStackSpillPhysicalSlot;
        m_spillSlotFreeList.clear();
#ifdef TESTBUILD
        for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs; i++)
        {
            m_gprValues[i].SetState(RegisterValue::State::Scratch);
        }
        for (size_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
        {
            m_fprValues[i].SetState(RegisterValue::State::Scratch);
        }
        m_valuesBorn.clear();
        m_valuesDead.clear();
        m_constantsMaterialized.clear();
        m_constantsDead.clear();
        m_inUseSpillSlotsMap.clear();
        m_freeSpillSlotsMap.clear();
        m_disallowAllocationOfNewSpillSlots = false;
#endif
    }

    // Many of the APIs of this class should only be used by RegAllocDecisionMaker
    //
    template<bool forGprState, typename T>
    friend struct RegAllocDecisionMaker;

private:
    // API expected by RegAllocDecisionMaker
    // Evict 'ssaVal' from 'regIdx'
    //
    // If 'dueToTakenByOutput' is true, it means the register is still holding an input operand right now,
    // but will hold an output operand after the node executes
    //
    template<bool forGprState>
    void EvictRegister(ValueRegAllocInfo* ssaVal, size_t regIdx, [[maybe_unused]] bool dueToTakenByOutput)
    {
        AssertValueLiveAndRegInfoOK(ssaVal);
        TestAssert(ssaVal->GetRegBankRegOrdInList<forGprState>() == regIdx);

        X64Reg reg = GetRegisterFromListOrd<forGprState>(regIdx);

        // Spill the value if needed
        //
        if (ssaVal->IsConstantLikeNodeOrIsSpilled())
        {
            // This value is a constant-like node (which never needs to be spilled) or is already spilled, no need to spill
            //
        }
        else if (ssaVal->HasNoMoreUseInBothGprAndFpr())
        {
            // This value has no more use, no need to spill
            //
        }
        else
        {
            // We need to spill this value
            //
            TestAssert(!ssaVal->IsSpilled());
            uint16_t spillSlot = AllocateSpillSlot();
            // Note that this must run before SetPhysicalSpillSlot
            //
            AssertShadowStackConsistencyForSSAValue(ssaVal);
            ssaVal->SetPhysicalSpillSlot(spillSlot);
            m_osrExitMap.SetForAllSlotsAssociatedWith(ssaVal->GetAssociatedShadowStackLocs(), spillSlot);
            EmitSpillInstruction(reg, spillSlot);
        }

        ssaVal->InvalidateRegBankRegister<forGprState>();

#ifdef TESTBUILD
        GetRegisterValue<forGprState>(regIdx).ProcessEvict(ssaVal, dueToTakenByOutput);
#endif
    }

    // API expected by RegAllocDecisionMaker
    // Relocate 'ssaVal' from 'fromRegIdx' to 'toRegIdx'
    //
    template<bool forGprState>
    void RelocateRegister(ValueRegAllocInfo* ssaVal, size_t fromRegIdx, size_t toRegIdx)
    {
        AssertValueLiveAndRegInfoOK(ssaVal);
        TestAssert(fromRegIdx != toRegIdx);
        TestAssert(ssaVal->GetRegBankRegOrdInList<forGprState>() == fromRegIdx);

        X64Reg fromReg = GetRegisterFromListOrd<forGprState>(fromRegIdx);
        X64Reg toReg = GetRegisterFromListOrd<forGprState>(toRegIdx);

        // If the SSA value is not a constant-like node and is not spilled yet,
        // this register may be the recovery location used by OSR exit. Thus, we must update all these records to the new location
        //
        // Note that if the SSA value is constant-like, the recovery location is always the constant identifier.
        // If the SSA value is spilled, the recovery location is always the spilled location. So we don't need to do anything in such cases.
        //
        if (ssaVal->IsNonConstantAndNotSpilled())
        {
            AssertShadowStackConsistencyForSSAValue(ssaVal);
            uint16_t dstPhysicalSlot = GetPhysicalSlotForRegisterFromListOrd<forGprState>(toRegIdx);
            m_osrExitMap.SetForAllSlotsAssociatedWith(ssaVal->GetAssociatedShadowStackLocs(), dstPhysicalSlot);
        }

        EmitRegRegMoveInstruction(fromReg, toReg);

        ssaVal->SetRegBankRegOrdInList<forGprState>(toRegIdx);

#ifdef TESTBUILD
        GetRegisterValue<forGprState>(fromRegIdx).ProcessRelocateFrom(ssaVal);
        GetRegisterValue<forGprState>(toRegIdx).ProcessRelocateTo(ssaVal);
#endif
    }

    // API expected by RegAllocDecisionMaker
    // Copy register value in 'fromRegIdx' to 'toRegIdx'
    //
    template<bool forGprState>
    void DuplicateRegister(size_t fromRegIdx, size_t toRegIdx)
    {
        TestAssert(fromRegIdx != toRegIdx);
        X64Reg fromReg = GetRegisterFromListOrd<forGprState>(fromRegIdx);
        X64Reg toReg = GetRegisterFromListOrd<forGprState>(toRegIdx);
        EmitRegRegMoveInstruction(fromReg, toReg);

#ifdef TESTBUILD
        AssertValueLiveAndRegInfoOK(GetRegisterValue<forGprState>(fromRegIdx).GetValue());
        GetRegisterValue<forGprState>(fromRegIdx).ProcessDuplicateFrom();
        GetRegisterValue<forGprState>(toRegIdx).ProcessDuplicateTo(GetRegisterValue<forGprState>(fromRegIdx).GetValue());
#endif
    }

    // API expected by RegAllocDecisionMaker
    // Load 'ssaVal' into 'regIdx'
    //
    template<bool forGprState>
    void LoadRegister(ValueRegAllocInfo* ssaVal, size_t regIdx)
    {
        AssertValueLiveAndRegInfoOK(ssaVal, true /*constantMaybeUnmaterialized*/);
        TestAssert(!ssaVal->IsAvailableInRegBank<forGprState>());

        X64Reg dstReg = GetRegisterFromListOrd<forGprState>(regIdx);

        // The value must be available somewhere: either a constant, or spilled, or in the other reg bank. Or we have a bug.
        //
        TestAssert(ssaVal->IsAvailableInRegBank<!forGprState>() || ssaVal->IsConstantLikeNode() || ssaVal->IsSpilled());

        if (ssaVal->IsAvailableInRegBank<!forGprState>())
        {
            // The value is already available in the other reg bank, the load can be accomplished by a reg-reg move
            //
            X64Reg srcReg = ssaVal->GetRegBankRegister<!forGprState>();
            EmitRegRegMoveInstruction(srcReg, dstReg);
        }
        else if (ssaVal->IsConstantLikeNode())
        {
            // This value is a constant-like value, emit materialization logic
            //
            EmitMaterializeConstantIntoRegister<forGprState>(ssaVal, regIdx);
        }
        else
        {
            TestAssert(ssaVal->IsSpilled());
            uint16_t spillSlot = ssaVal->GetPhysicalSpillSlot();
            EmitLoadInstruction(spillSlot, dstReg);
        }

        ssaVal->SetRegBankRegOrdInList<forGprState>(regIdx);

#ifdef TESTBUILD
        GetRegisterValue<forGprState>(regIdx).ProcessLoad(ssaVal);
#endif
    }

    // API expected by RegAllocDecisionMaker
    // 'ssaVal' is dead while register 'regIdx' is holding it.
    //
    template<bool forGprState>
    void KillRegister(ValueRegAllocInfo* ssaVal, [[maybe_unused]] size_t regIdx)
    {
        AssertValueLiveAndRegInfoOK(ssaVal);
        TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        TestAssert(ssaVal->GetRegBankRegOrdInList<forGprState>() == regIdx);
        ssaVal->InvalidateRegBankRegister<forGprState>();

#ifdef TESTBUILD
        GetRegisterValue<forGprState>(regIdx).ProcessDeath(ssaVal);
#endif
    }

public:
    // Process the death event of an SSA value
    // A bit ugly: this method must be called at last, after calling KillRegister() if the ssaVal is also available in registers
    //
    void ProcessDeath(ValueRegAllocInfo* ssaVal)
    {
        AssertValueLiveAndRegInfoOK(ssaVal);
        // Registers must have been declared dead by KillRegister
        //
        TestAssert(!ssaVal->IsAvailableInGPR());
        TestAssert(!ssaVal->IsAvailableInFPR());
        TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());

        if (!ssaVal->IsConstantLikeNode() && ssaVal->IsSpilled())
        {
            // This value has a spill location, this spill location is free now
            //
            uint16_t spillSlot = ssaVal->GetPhysicalSpillSlot();
            DeallocateSpillSlot(spillSlot);
        }

#ifdef TESTBUILD
        if (ssaVal->IsConstantLikeNode())
        {
            TestAssert(m_constantsMaterialized.count(ssaVal));
            TestAssert(!m_constantsDead.count(ssaVal));
            m_constantsDead.insert(ssaVal);
        }
        else
        {
            TestAssert(m_valuesBorn.count(ssaVal));
            TestAssert(!m_valuesDead.count(ssaVal));
            m_valuesDead.insert(ssaVal);
        }
#endif
    }

    // Process the born event (as an output of a node) of an SSA value in a register
    //
    template<bool forGprState>
    void ProcessBornInRegister(ValueRegAllocInfo* ssaVal, size_t regIdx)
    {
        TestAssert(!ssaVal->IsConstantLikeNode());
        TestAssert(!ssaVal->IsAvailableInGPR());
        TestAssert(!ssaVal->IsAvailableInFPR());
        TestAssert(!ssaVal->IsSpilled());

        ssaVal->SetRegBankRegOrdInList<forGprState>(regIdx);

#ifdef TESTBUILD
        GetRegisterValue<forGprState>(regIdx).ProcessBorn(ssaVal);
        TestAssert(!m_valuesBorn.count(ssaVal));
        m_valuesBorn.insert(ssaVal);
#endif
    }

    // Process the born event (as an output of a node) of an SSA value on the stack
    // Note that the born location also becomes the spill location of the SSA value
    //
    void ProcessBornOnStack(ValueRegAllocInfo* ssaVal, uint16_t physicalSlot)
    {
        TestAssert(!ssaVal->IsConstantLikeNode());
        TestAssert(!ssaVal->IsAvailableInGPR());
        TestAssert(!ssaVal->IsAvailableInFPR());
        TestAssert(!ssaVal->IsSpilled());
        TestAssert(m_firstStackSpillPhysicalSlot <= physicalSlot && physicalSlot < m_totalNumPhysicalSlots);

#ifdef TESTBUILD
        // This slot should not be accounted for at this moment, so it should show up in neither the free list nor the in-use list now
        //
        TestAssert(!m_freeSpillSlotsMap.count(physicalSlot));
        TestAssert(!m_inUseSpillSlotsMap.count(physicalSlot));
        m_inUseSpillSlotsMap.insert(physicalSlot);
        TestAssert(!m_valuesBorn.count(ssaVal));
        m_valuesBorn.insert(ssaVal);
#endif

        ssaVal->SetPhysicalSpillSlot(physicalSlot);
    }

    // Process the effect of ShadowStore(ssaVal, slot)
    //
    void ProcessShadowStore(ValueRegAllocInfo* ssaVal, InterpreterSlot interpreterSlot)
    {
        AssertValueLiveAndRegInfoOK(ssaVal, true /*constantMaybeUnmaterialized*/);
        uint16_t shadowStackSlot = SafeIntegerCast<uint16_t>(interpreterSlot.Value());
        if (ssaVal->IsConstantLikeNode())
        {
            // The shadow stack location now stores a constant, so it is not associated with any SSA value
            //
            m_osrExitMap.SetAndDeassociate(shadowStackSlot, ssaVal->GetConstantIdentifier());
#ifdef TESTBUILD
            // Treat it as if the constant has materialized, to avoid false assertion when the constant becomes dead
            //
            m_constantsMaterialized.insert(ssaVal);
#endif
        }
        else
        {
            uint16_t ident;
            // If the SSA value has been spilled, we should recover the value using the spilled location
            // since it will be always available until the SSA value's death and never moves
            //
            if (ssaVal->IsSpilled())
            {
                ident = ssaVal->GetPhysicalSpillSlot();
            }
            else
            {
                // Otherwise, we should recover the value from the register that holds its value
                // If it is available in both GPR and FPR, any works
                //
                if (ssaVal->IsAvailableInFPR())
                {
                    size_t regIdx = ssaVal->GetFprOrdInList();
                    ident = GetPhysicalSlotForRegisterFromListOrd<false /*forGprState*/>(regIdx);
                }
                else
                {
                    TestAssert(ssaVal->IsAvailableInGPR());
                    size_t regIdx = ssaVal->GetGprOrdInList();
                    ident = GetPhysicalSlotForRegisterFromListOrd<true /*forGprState*/>(regIdx);
                }
            }
            // The shadow stack location is now associated with this SSA value,
            // and needs to be updated whenever the SSA value is relocated
            // (though this may only happen if the SSA value is not spilled yet)
            //
            m_osrExitMap.SetAndAssociate(ssaVal->GetAssociatedShadowStackLocs(), shadowStackSlot, ident);
        }
    }

    // Assert that 'shadowStackSlot' is currently associated with 'ssaVal' and indeed recovers to 'ssaVal'
    // 'ssaVal' must not be a constant-like node
    //
    void AssertShadowStackSlotAssociatedWithSSAValue([[maybe_unused]] uint16_t shadowStackSlot, [[maybe_unused]] ValueRegAllocInfo* ssaVal)
    {
#ifdef TESTBUILD
        TestAssert(!ssaVal->IsConstantLikeNode());
        uint16_t currentVal = m_osrExitMap.QueryCurrentValue(shadowStackSlot);
        bool checkOk = false;
        if (ssaVal->IsSpilled())
        {
            // If the SSA value is spilled, the shadow stack slot MUST be holding the spilled location,
            // since we always update all shadow stack slots when we spill an SSA value,
            // and the spill location always takes precedence over registers for any future use
            //
            TestAssert(currentVal == ssaVal->GetPhysicalSpillSlot());
            checkOk = true;
        }
        if (ssaVal->IsAvailableInGPR())
        {
            if (currentVal == GetPhysicalSlotForRegisterFromListOrd<true /*forGprState*/>(ssaVal->GetGprOrdInList()))
            {
                checkOk = true;
            }
        }
        if (ssaVal->IsAvailableInFPR())
        {
            if (currentVal == GetPhysicalSlotForRegisterFromListOrd<false /*forGprState*/>(ssaVal->GetFprOrdInList()))
            {
                checkOk = true;
            }
        }
        TestAssert(checkOk && "unexpected value is stored in the shadow stack slot");

        // Also assert that 'shadowStackSlot' is indeed on ssaVal's doubly-linked list of associated slots
        //
        m_osrExitMap.AssertSlotIsAssociatedWithValue(ssaVal->GetAssociatedShadowStackLocs(), shadowStackSlot);
#endif
    }

    // Assert that all values on the shadow stack that is associated with 'ssaVal' indeed recovers to 'ssaVal'
    // 'ssaVal' must not be a constant-like node
    //
    void AssertShadowStackConsistencyForSSAValue([[maybe_unused]] ValueRegAllocInfo* ssaVal)
    {
#ifdef TESTBUILD
        TestAssert(!ssaVal->IsConstantLikeNode());
        m_osrExitMap.ForEachSlotAssociatedWith(
            ssaVal->GetAssociatedShadowStackLocs(),
            [&](uint16_t shadowStackSlot) ALWAYS_INLINE
            {
                AssertShadowStackSlotAssociatedWithSSAValue(shadowStackSlot, ssaVal);
            });
#endif
    }

    // Process SetLocal, note that physicalSlotForLocal is the physical slot, not the VirtualRegister
    //
    void ProcessSetLocal(ValueRegAllocInfo* ssaVal, InterpreterSlot interpreterSlot, uint16_t physicalSlotForLocal)
    {
        AssertValueLiveAndRegInfoOK(ssaVal);
        TestAssert(m_firstLocalPhysicalSlot <= physicalSlotForLocal && physicalSlotForLocal < m_firstStackSpillPhysicalSlot);

        uint16_t shadowStackSlot = SafeIntegerCast<uint16_t>(interpreterSlot.Value());
        if (ssaVal->IsConstantLikeNode())
        {
            // The shadow stack location must be storing the same constant, nothing to do
            //
            // DEVNOTE: our DFG OSR exit works by having a consistent global mapping at each BB start,
            // and the log only logs what happens inside each BB. This makes it fine to not update the shadow stack slot
            // content to the local's physical slot if the value is a constant, since they are the same thing *inside this BB*,
            // and in another BB, the source of truth will always be the consistent global mapping which points to the local.
            //
            // However, if we want to get rid of the consistent global mapping (to support global reg alloc, for example),
            // and use heavy-light-decomposition on the global spanning tree to do OSR exit,
            // we *will* need to update the shadow stack slot content to the local's physical slot,
            // since we must guarantee that all possible "log paths" that reaches a BB converge at the same state
            // (e.g., BB1->BB3, BB2->BB3, it may be the case that in BB1, a constant is stored into DfgLocal 1 / InterpreterSlot 1,
            // but in BB2 a non-constant value is stored into DfgLocal 1 / InterpreterSlot 1).
            //
            TestAssert(m_osrExitMap.QueryCurrentValue(shadowStackSlot) == ssaVal->GetConstantIdentifier());
        }
        else
        {
            // The shadow stack location must be associated with the SSA value and storing something that recovers to this SSA value
            //
            AssertShadowStackSlotAssociatedWithSSAValue(shadowStackSlot, ssaVal);

            // Now, the shadow stack slot should be updated to the physical slot for the local, since the SSA value may die before the BB end.
            // Note that in block-local SSA form, in each BB there can be at most one SetLocal to each local,
            // so the local value will never be overridden until the end of the BB.
            //
            // The shadow stack slot is also no longer associated with the SSA value (since it's pointing to a local now)
            //
            m_osrExitMap.SetAndDeassociate(shadowStackSlot, physicalSlotForLocal);
        }
    }

    // Allocate a spill slot for an SSA value that borns on the stack (as an output of a node, due to the node not supporting reg alloc),
    // or for a temporary value on the stack.
    //
    // In both cases, the returned slot is *not* accounted for (i.e., not free and not used in our record),
    // and caller logic must clean it up appropriately afterwards:
    // 1. For SSA born, the born does not happen right now. After executing the node, caller must use ProcessBornOnStack,
    //    at which point the value is actually born.
    // 2. For temporary value, caller logic must free it.
    //
    uint16_t WARN_UNUSED AllocateSpillSlotForTemporaryOrSSABornOnStack()
    {
        uint16_t spillSlot = AllocateSpillSlot();
#ifdef TESTBUILD
        // Remove the slot from the in-use list, since the born hasn't happen yet
        //
        TestAssert(m_inUseSpillSlotsMap.count(spillSlot));
        m_inUseSpillSlotsMap.erase(m_inUseSpillSlotsMap.find(spillSlot));
#endif
        return spillSlot;
    }

    // Reserve at least the given number of spill slots,
    // so it is guaranteed that we will not allocate new slots at frame end before these slots are all consumed
    //
    void ReserveSpillSlots(size_t desiredNumSlots)
    {
        TestAssert(!m_disallowAllocationOfNewSpillSlots);
        AssertAllSpillSlotsAreAccounted();
        if (m_spillSlotFreeList.size() < desiredNumSlots)
        {
            size_t numNeeded = desiredNumSlots - m_spillSlotFreeList.size();
            uint16_t newTotalNumPhysicalSlots = static_cast<uint16_t>(m_totalNumPhysicalSlots + numNeeded);
            for (uint16_t slotOrd = m_totalNumPhysicalSlots; slotOrd < newTotalNumPhysicalSlots; slotOrd++)
            {
                m_spillSlotFreeList.push_back(slotOrd);
#ifdef TESTBUILD
                TestAssert(!m_freeSpillSlotsMap.count(slotOrd));
                m_freeSpillSlotsMap.insert(slotOrd);
#endif
            }
            m_totalNumPhysicalSlots = newTotalNumPhysicalSlots;
            m_historyMaxNumPhysicalSlots = std::max(m_historyMaxNumPhysicalSlots, m_totalNumPhysicalSlots);
            AssertAllSpillSlotsAreAccounted();
        }
        TestAssert(m_spillSlotFreeList.size() >= desiredNumSlots);
    }

    // Allocate a range of slots on the stack at the end of the current stack frame, return the start physical slot of the range.
    // All slots in the allocated range are not accounted for (show up in neither freeList nor inUseList).
    //
    // Note that for nodes that takes a range operand, they may clobber everything to the right of the range
    // Thus, the range must be allocated at the end of the stack frame,
    // and the caller side must reserve enough spill slots for all potential register operations before allocating the range
    // To enforce this, calling this function will set a flag that triggers assertion on allocating new spill slots
    // and caller must explicitly reset this flag after the code for the node is generated
    //
    uint16_t WARN_UNUSED AllocatePhysicalRange(uint16_t desiredNumSlots)
    {
#ifdef TESTBUILD
        TestAssert(!m_disallowAllocationOfNewSpillSlots);
        m_disallowAllocationOfNewSpillSlots = true;
#endif
        AssertAllSpillSlotsAreAccounted();
        uint16_t res = m_totalNumPhysicalSlots;
        m_totalNumPhysicalSlots += desiredNumSlots;
        m_historyMaxNumPhysicalSlots = std::max(m_historyMaxNumPhysicalSlots, m_totalNumPhysicalSlots);
        return res;
    }

    // Return the length of the physical stack frame right now
    //
    uint16_t WARN_UNUSED GetCurrentTotalNumberOfPhysicalSlots()
    {
        return m_totalNumPhysicalSlots;
    }

    // Return the max length of the physical stack frame ever reached
    //
    uint16_t WARN_UNUSED GetMaxTotalNumberOfPhysicalSlots()
    {
        return m_historyMaxNumPhysicalSlots;
    }

    // Mark an unaccounted physical slot as free. This may be a temporary slot,
    // or a slot in the range operand that does not hold an output value after node execution.
    // Note that in both cases, the slot is not accounted for.
    // This function marks the given slot as free so it can be reused
    //
    void MarkUnaccountedPhysicalSlotAsFree(uint16_t physicalSlot)
    {
        TestAssert(m_firstStackSpillPhysicalSlot <= physicalSlot && physicalSlot < m_totalNumPhysicalSlots);
        TestAssert(m_freeSpillSlotsMap.size() == m_spillSlotFreeList.size());
#ifdef TESTBUILD
        // This slot must be not accounted for right now, so should not show up in inUseList or freeList
        //
        TestAssert(!m_inUseSpillSlotsMap.count(physicalSlot));
        TestAssert(!m_freeSpillSlotsMap.count(physicalSlot));
        m_freeSpillSlotsMap.insert(physicalSlot);
#endif
        m_spillSlotFreeList.push_back(physicalSlot);
    }

    // After the node that takes a range operand finishes exeuction, some of the values in the range will now hold outputs,
    // others become scratch. Since the range operand always reside at the end of the frame, we can shrink the end of the frame
    // to the last valid output. This should always be paired with 'AllocatePhysicalRange',
    // and it clears the assert-on-spill flag set by AllocatePhysicalRange
    //
    void ShrinkPhysicalFrameLength([[maybe_unused]] uint16_t expectedCurrentSize, uint16_t newSize)
    {
#ifdef TESTBUILD
        TestAssert(m_disallowAllocationOfNewSpillSlots);
        m_disallowAllocationOfNewSpillSlots = false;
#endif
        TestAssert(m_totalNumPhysicalSlots == expectedCurrentSize);
        TestAssert(newSize <= expectedCurrentSize);
        TestAssertImp(!m_freeSpillSlotsMap.empty(), *(--m_freeSpillSlotsMap.end()) < newSize);
        TestAssertImp(!m_inUseSpillSlotsMap.empty(), *(--m_inUseSpillSlotsMap.end()) < newSize);
        m_totalNumPhysicalSlots = newSize;
    }

    // Assert that each spill slot is either in the in-use list, or in the free list
    // This invariant should hold at the start and end of each node
    //
    void AssertAllSpillSlotsAreAccounted()
    {
        TestAssert(m_freeSpillSlotsMap.size() + m_inUseSpillSlotsMap.size() == m_totalNumPhysicalSlots - m_firstStackSpillPhysicalSlot);
        TestAssertImp(!m_freeSpillSlotsMap.empty(), *m_freeSpillSlotsMap.begin() >= m_firstStackSpillPhysicalSlot);
        TestAssertImp(!m_freeSpillSlotsMap.empty(), *(--m_freeSpillSlotsMap.end()) < m_totalNumPhysicalSlots);
        TestAssertImp(!m_inUseSpillSlotsMap.empty(), *m_inUseSpillSlotsMap.begin() >= m_firstStackSpillPhysicalSlot);
        TestAssertImp(!m_inUseSpillSlotsMap.empty(), *(--m_inUseSpillSlotsMap.end()) < m_totalNumPhysicalSlots);
        TestAssert(m_spillSlotFreeList.size() == m_freeSpillSlotsMap.size());
    }

    // Materialize a constant into x_dfg_custom_purpose_temp_reg, does not affect reg alloc state
    //
    void MaterializeConstantToTempReg(ValueRegAllocInfo* val)
    {
        TestAssert(val->IsConstantLikeNode());

#ifdef TESTBUILD
        // It is possible that a constant is materialized multiple times,
        // but as long as it is used, even if all its uses are by this function,
        // we expect ProcessDeath to be called, so we should add it to m_constantsMaterialized.
        //
        m_constantsMaterialized.insert(val);
        TestAssert(!m_constantsDead.count(val));
#endif

        m_codegenLog.EmitMaterializeConstantLikeNodeToTempReg(val->GetConstantLikeNode());
    }

    CodegenOperationLog& WARN_UNUSED GetCodegenLog()
    {
        return m_codegenLog;
    }

    // Assert that ssaVal is live and if it claims that it is in register, its record agrees with ours
    //
    void AssertValueLiveAndRegInfoOK([[maybe_unused]] ValueRegAllocInfo* ssaVal, [[maybe_unused]] bool constantMaybeUnmaterialized = false)
    {
#ifdef TESTBUILD
        if (ssaVal->IsConstantLikeNode())
        {
            TestAssertImp(!constantMaybeUnmaterialized, m_constantsMaterialized.count(ssaVal));
            TestAssert(!m_constantsDead.count(ssaVal));
        }
        else
        {
            TestAssert(m_valuesBorn.count(ssaVal));
            TestAssert(!m_valuesDead.count(ssaVal));
            if (ssaVal->IsSpilled())
            {
                uint16_t spillSlot = ssaVal->GetPhysicalSpillSlot();
                TestAssert(m_firstStackSpillPhysicalSlot <= spillSlot && spillSlot < m_totalNumPhysicalSlots);
                TestAssert(!m_freeSpillSlotsMap.count(spillSlot));
                TestAssert(m_inUseSpillSlotsMap.count(spillSlot));
            }
        }
        if (ssaVal->IsAvailableInGPR())
        {
            RegisterValue& regVal = GetRegisterValue<true /*forGprState*/>(ssaVal->GetGprOrdInList());
            TestAssert(regVal.GetState() == RegisterValue::State::InUse);
            TestAssert(regVal.GetValue() == ssaVal);
        }
        if (ssaVal->IsAvailableInFPR())
        {
            RegisterValue& regVal = GetRegisterValue<false /*forGprState*/>(ssaVal->GetFprOrdInList());
            TestAssert(regVal.GetState() == RegisterValue::State::InUse);
            TestAssert(regVal.GetValue() == ssaVal);
        }
#endif
    }

    // Assert that the given register is in Ephemeral state (i.e., will be replaced by output) or Duplication state, and holding ssaVal
    // Note that Duplication state is possible, since it's possible that an output takes the position of a duplication.
    //
    template<bool forGprState>
    void AssertRegisterEphemerallyHoldingValue([[maybe_unused]] ValueRegAllocInfo* ssaVal, [[maybe_unused]] size_t regIdx)
    {
#ifdef TESTBUILD
        AssertValueLiveAndRegInfoOK(ssaVal);
        RegisterValue& regVal = GetRegisterValue<forGprState>(regIdx);
        TestAssert(regVal.GetState() == RegisterValue::State::Ephemeral || regVal.GetState() == RegisterValue::State::Duplication);
        TestAssert(regVal.GetValue() == ssaVal);
        TestAssertImp(regVal.GetState() == RegisterValue::State::Ephemeral, !ssaVal->IsAvailableInRegBank<forGprState>());
#endif
    }

    // Assert that the given register is in InUse or Duplication state and holding ssaVal
    //
    template<bool forGprState>
    void AssertRegisterHoldingValue([[maybe_unused]] ValueRegAllocInfo* ssaVal, [[maybe_unused]] size_t regIdx)
    {
#ifdef TESTBUILD
        // Note that it may be possible that ssaVal is not in register, since the owning register has transitioned to Ephemeral state
        //
        AssertValueLiveAndRegInfoOK(ssaVal);
        RegisterValue& regVal = GetRegisterValue<forGprState>(regIdx);
        TestAssert(regVal.GetState() == RegisterValue::State::InUse || regVal.GetState() == RegisterValue::State::Duplication);
        TestAssert(regVal.GetValue() == ssaVal);
        TestAssertImp(regVal.GetState() == RegisterValue::State::InUse, ssaVal->GetRegBankRegOrdInList<forGprState>() == regIdx);
#endif
    }

    // Assert that no register is holding a valid value
    //
    void AssertAllRegistersScratched()
    {
#ifdef TESTBUILD
        for (size_t regIdx = 0; regIdx < x_dfg_reg_alloc_num_gprs; regIdx++)
        {
            TestAssert(m_gprValues[regIdx].GetState() == RegisterValue::State::Scratch ||
                       m_gprValues[regIdx].GetState() == RegisterValue::State::Duplication);
        }
        for (size_t regIdx = 0; regIdx < x_dfg_reg_alloc_num_fprs; regIdx++)
        {
            TestAssert(m_fprValues[regIdx].GetState() == RegisterValue::State::Scratch ||
                       m_fprValues[regIdx].GetState() == RegisterValue::State::Duplication);
        }
#endif
    }

    // Our model is that duplication registers are only used for the node execution it is prepared for,
    // and automatically becomes scratch afterwards (even if physically it may still be valid right after node execution)
    // Transit duplication to scratch after each node execution, and assert that no ephemeral state exists.
    //
    // Note that this is only used for assertion only, since this class is not responsible for managing
    // register state, and the variables m_gprValues/m_fprValues below only exist in test build for assertions
    //
    void UpdateRegisterStateAfterNodeExecutedAndOutputsBorn()
    {
#ifdef TESTBUILD
        for (size_t regIdx = 0; regIdx < x_dfg_reg_alloc_num_gprs; regIdx++)
        {
            TestAssert(m_gprValues[regIdx].GetState() != RegisterValue::State::Ephemeral);
            if (m_gprValues[regIdx].GetState() == RegisterValue::State::Duplication)
            {
                m_gprValues[regIdx].SetState(RegisterValue::State::Scratch);
            }
        }
        for (size_t regIdx = 0; regIdx < x_dfg_reg_alloc_num_fprs; regIdx++)
        {
            TestAssert(m_fprValues[regIdx].GetState() != RegisterValue::State::Ephemeral);
            if (m_fprValues[regIdx].GetState() == RegisterValue::State::Duplication)
            {
                m_fprValues[regIdx].SetState(RegisterValue::State::Scratch);
            }
        }
#endif
    }

    void AssertConsistencyAtBasicBlockEnd()
    {
#ifdef TESTBUILD
        // All registers should be scratch or duplicate
        //
        AssertAllRegistersScratched();
        // All born SSA values are dead, not in any register, and has no more uses
        //
        TestAssert(m_valuesBorn.size() == m_valuesDead.size());
        for (ValueRegAllocInfo* ssaVal : m_valuesBorn)
        {
            TestAssert(!ssaVal->IsConstantLikeNode());
            TestAssert(m_valuesDead.count(ssaVal));
            TestAssert(!ssaVal->IsAvailableInGPR());
            TestAssert(!ssaVal->IsAvailableInFPR());
            TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        }
        TestAssert(m_constantsMaterialized.size() == m_constantsDead.size());
        for (ValueRegAllocInfo* ssaVal : m_constantsMaterialized)
        {
            TestAssert(ssaVal->IsConstantLikeNode());
            TestAssert(m_constantsDead.count(ssaVal));
            TestAssert(!ssaVal->IsAvailableInGPR());
            TestAssert(!ssaVal->IsAvailableInFPR());
            TestAssert(ssaVal->HasNoMoreUseInBothGprAndFpr());
        }
        // All spill slots are accounted for
        //
        TestAssert(!m_disallowAllocationOfNewSpillSlots);
        AssertAllSpillSlotsAreAccounted();
        // All spill slots are empty, and the free list indeed consist of all the spill slots
        //
        TestAssert(m_inUseSpillSlotsMap.empty());
        TestAssert(m_freeSpillSlotsMap.size() == m_totalNumPhysicalSlots - m_firstStackSpillPhysicalSlot);
        {
            size_t curSlot = m_firstStackSpillPhysicalSlot;
            for (uint16_t val : m_freeSpillSlotsMap)
            {
                TestAssert(val == curSlot);
                curSlot++;
            }
            TestAssert(curSlot == m_totalNumPhysicalSlots);
        }
        {
            TestAssert(m_spillSlotFreeList.size() == m_freeSpillSlotsMap.size());
            TempVector<uint16_t> tmp = m_spillSlotFreeList;
            std::sort(tmp.begin(), tmp.end());
            for (size_t i = 0; i < tmp.size(); i++)
            {
                TestAssert(tmp[i] == m_firstStackSpillPhysicalSlot + i);
            }
        }
#endif
    }

private:
    void EmitSpillInstruction(X64Reg reg, uint16_t spillSlot)
    {
        m_codegenLog.EmitRegSpill(reg, spillSlot);
    }

    void EmitRegRegMoveInstruction(X64Reg srcReg, X64Reg dstReg)
    {
        m_codegenLog.EmitRegRegMove(srcReg, dstReg);
    }

    void EmitLoadInstruction(uint16_t spillSlot, X64Reg reg)
    {
        m_codegenLog.EmitRegLoad(spillSlot, reg);
    }

    template<bool forGprState>
    void EmitMaterializeConstantIntoRegister(ValueRegAllocInfo* cst, size_t regIdx)
    {
        TestAssert(cst->IsConstantLikeNode());
#ifdef TESTBUILD
        // It is possible that a constant is materialized multiple times.
        //
        m_constantsMaterialized.insert(cst);
        TestAssert(!m_constantsDead.count(cst));
#endif
        m_codegenLog.EmitMaterializeConstantLikeNodeToRegAllocReg<forGprState>(cst->GetConstantLikeNode(), regIdx);
    }

    uint16_t WARN_UNUSED AllocateSpillSlot()
    {
        TestAssert(m_spillSlotFreeList.size() == m_freeSpillSlotsMap.size());
        if (m_spillSlotFreeList.size() > 0)
        {
            uint16_t res = m_spillSlotFreeList.back();
            m_spillSlotFreeList.pop_back();
#ifdef TESTBUILD
            TestAssert(m_freeSpillSlotsMap.count(res));
            m_freeSpillSlotsMap.erase(m_freeSpillSlotsMap.find(res));
            TestAssert(!m_inUseSpillSlotsMap.count(res));
            m_inUseSpillSlotsMap.insert(res);
#endif
            TestAssert(m_firstStackSpillPhysicalSlot <= res && res < m_totalNumPhysicalSlots);
            return res;
        }

        // We are allocating a new spill slot at the end of the frame.
        // We must be allowed to do this at this moment, or it's a bug.
        //
        TestAssert(!m_disallowAllocationOfNewSpillSlots);
        uint16_t res = m_totalNumPhysicalSlots;
        m_totalNumPhysicalSlots++;
        m_historyMaxNumPhysicalSlots = std::max(m_historyMaxNumPhysicalSlots, m_totalNumPhysicalSlots);
#ifdef TESTBUILD
        TestAssert(!m_freeSpillSlotsMap.count(res));
        TestAssertImp(!m_freeSpillSlotsMap.empty(), *(--m_freeSpillSlotsMap.end()) < res);
        TestAssert(!m_inUseSpillSlotsMap.count(res));
        m_inUseSpillSlotsMap.insert(res);
        TestAssert(*(--m_inUseSpillSlotsMap.end()) == res);
#endif
        return res;
    }

    void DeallocateSpillSlot(uint16_t spillSlot)
    {
        TestAssert(m_firstStackSpillPhysicalSlot <= spillSlot && spillSlot < m_totalNumPhysicalSlots);
        TestAssert(m_spillSlotFreeList.size() == m_freeSpillSlotsMap.size());
#ifdef TESTBUILD
        TestAssert(m_inUseSpillSlotsMap.count(spillSlot));
        m_inUseSpillSlotsMap.erase(m_inUseSpillSlotsMap.find(spillSlot));
        TestAssert(!m_freeSpillSlotsMap.count(spillSlot));
        m_freeSpillSlotsMap.insert(spillSlot);
#endif
        m_spillSlotFreeList.push_back(spillSlot);
    }

    template<bool forGprState>
    static X64Reg WARN_UNUSED GetRegisterFromListOrd(size_t regIdx)
    {
        if (forGprState)
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
            return x_dfg_reg_alloc_gprs[regIdx];
        }
        else
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
            return x_dfg_reg_alloc_fprs[regIdx];
        }
    }

    template<bool forGprState>
    uint16_t WARN_UNUSED GetPhysicalSlotForRegisterFromListOrd(size_t regIdx)
    {
        if (forGprState)
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
            return static_cast<uint16_t>(m_firstGprRegisterPhysicalSlot + regIdx);
        }
        else
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
            return static_cast<uint16_t>(m_firstFprRegisterPhysicalSlot + regIdx);
        }
    }

    DfgOsrExitMapBuilder m_osrExitMap;
    CodegenOperationLog m_codegenLog;
    uint16_t m_firstGprRegisterPhysicalSlot;
    uint16_t m_firstFprRegisterPhysicalSlot;
    uint16_t m_firstLocalPhysicalSlot;
    uint16_t m_firstStackSpillPhysicalSlot;
    uint16_t m_totalNumPhysicalSlots;
    uint16_t m_historyMaxNumPhysicalSlots;
    TempVector<uint16_t> m_spillSlotFreeList;

#ifdef TESTBUILD
    // In test build, also do our own tracking of the register values to confirm that the register operations are valid
    //
    struct RegisterValue
    {
        enum class State
        {
            // This register is not holding an SSA value
            //
            Scratch,
            // This register is holding a duplication of an SSA value.
            // The SSA value is not logically associated with this register,
            // so the register can be directly used as scratch or to hold an output,
            // and Evict should never be called on this register
            //
            Duplication,
            // This register is holding an input SSA value right now, but will be used to hold an output
            // Evict has been called to reach this state, so it should not be evicted again
            //
            Ephemeral,
            // This register is holding an SSA value and is logically associated with it
            //
            InUse
        };

        RegisterValue() : m_compositeVal(0) { TestAssert(GetState() == State::Scratch); }

        State GetState() { return static_cast<State>(m_compositeVal & x_stateMask); }

        void SetState(State state)
        {
            uint64_t stateVal = static_cast<uint64_t>(state);
            TestAssert(stateVal <= x_stateMask);
            m_compositeVal &= (~x_stateMask);
            m_compositeVal |= stateVal;
            TestAssert(GetState() == state);
        }

        ValueRegAllocInfo* GetValue()
        {
            TestAssert(GetState() != State::Scratch);
            return reinterpret_cast<ValueRegAllocInfo*>(m_compositeVal & (~x_stateMask));
        }

        void SetValue(ValueRegAllocInfo* value)
        {
            TestAssert(value != nullptr);
            uint64_t stateVal = m_compositeVal & x_stateMask;
            uint64_t ptrVal = reinterpret_cast<uint64_t>(value);
            TestAssert((ptrVal & x_stateMask) == 0);
            m_compositeVal = ptrVal | stateVal;
        }

        void ProcessEvict(ValueRegAllocInfo* expectedValue, bool dueToTakenByOutput)
        {
            // The only state where it is allowed to call Evict is InUse
            //
            TestAssert(GetState() == State::InUse);
            TestAssert(GetValue() == expectedValue);
            if (dueToTakenByOutput)
            {
                SetState(State::Ephemeral);
            }
            else
            {
                SetState(State::Scratch);
            }
        }

        void ProcessRelocateFrom(ValueRegAllocInfo* expectedValue)
        {
            // The only state where one can relocate this register to another register is InUse
            //
            TestAssert(GetState() == State::InUse);
            TestAssert(GetValue() == expectedValue);
            // After relocation this register becomes scratch (technically, it is still physically holding the value,
            // but currently we will never use it for any purpose)
            //
            SetState(State::Scratch);
        }

        void ProcessRelocateTo(ValueRegAllocInfo* value)
        {
            // Ephemeral should become output after node execution, so the destination should not see this
            //
            TestAssert(GetState() == State::Scratch || GetState() == State::Duplication);
            SetState(State::InUse);
            SetValue(value);
        }

        void ProcessDuplicateFrom()
        {
            TestAssert(GetState() == State::InUse);
        }

        void ProcessDuplicateTo(ValueRegAllocInfo* value)
        {
            // Ephemeral should become output after node execution, so the destination should not see this
            //
            TestAssert(GetState() == State::Scratch || GetState() == State::Duplication);
            SetState(State::Duplication);
            SetValue(value);
        }

        void ProcessLoad(ValueRegAllocInfo* value)
        {
            // Ephemeral should become output after node execution, so the destination should not see this
            //
            TestAssert(GetState() == State::Scratch || GetState() == State::Duplication);
            SetState(State::InUse);
            SetValue(value);
        }

        void ProcessBorn(ValueRegAllocInfo* value)
        {
            // Note that it is possible that an output is born into a register that was originally a duplication
            //
            TestAssert(GetState() == State::Scratch || GetState() == State::Duplication || GetState() == State::Ephemeral);
            SetState(State::InUse);
            SetValue(value);
        }

        void ProcessDeath(ValueRegAllocInfo* value)
        {
            TestAssert(GetState() == State::InUse);
            TestAssert(GetValue() == value);
            SetState(State::Scratch);
        }

        static constexpr size_t x_numBitsForState = 2;
        static constexpr uint64_t x_stateMask = static_cast<uint64_t>((1U << x_numBitsForState) - 1);

        static_assert(alignof(ValueRegAllocInfo) >= (1U << x_numBitsForState), "required for pointer tagging");

        // A tagged pointer, with the lower bits stolen to store State
        //
        uint64_t m_compositeVal;
    };

    template<bool forGprState>
    RegisterValue& WARN_UNUSED GetRegisterValue(size_t regIdx)
    {
        if (forGprState)
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_gprs);
            return m_gprValues[regIdx];
        }
        else
        {
            TestAssert(regIdx < x_dfg_reg_alloc_num_fprs);
            return m_fprValues[regIdx];
        }
    }

    RegisterValue m_gprValues[x_dfg_reg_alloc_num_gprs];
    RegisterValue m_fprValues[x_dfg_reg_alloc_num_fprs];
    // Track and validate that every born SSA value is eventually dead,
    // and every materialized constant is eventually dead
    //
    TempUnorderedSet<ValueRegAllocInfo*> m_valuesBorn;
    TempUnorderedSet<ValueRegAllocInfo*> m_valuesDead;
    TempUnorderedSet<ValueRegAllocInfo*> m_constantsMaterialized;
    TempUnorderedSet<ValueRegAllocInfo*> m_constantsDead;
    // Track the locations of all in-use and free spill slots
    //
    TempSet<uint16_t> m_inUseSpillSlotsMap;
    TempSet<uint16_t> m_freeSpillSlotsMap;
    bool m_disallowAllocationOfNewSpillSlots;
#endif
};

}   // namespace dfg
