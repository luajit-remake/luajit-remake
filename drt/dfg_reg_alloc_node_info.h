#pragma once

#include "common_utils.h"
#include "dfg_reg_alloc_register_info.h"
#include "temp_arena_allocator.h"

namespace dfg {

// This class records:
// 1. Where a SSA value is available: it could be in GPR and/or in FPR and/or on the stack.
// 2. Where is the next use of this SSA value
//
struct alignas(8) ValueRegAllocInfo
{
    ValueRegAllocInfo()
        : m_compositeVal(0)
        , m_nextUseInputOrd(static_cast<uint16_t>(-1))
        , m_physicalSpillLoc(static_cast<uint16_t>(-1))
    {
        InvalidateGPR();
        Assert(!IsAvailableInGPR() && !IsAvailableInFPR() && !IsSpilled() && !HasNextUse());
    }

    // RSP is never a valid reg for reg alloc, so use it to indicate invalid
    //
    static constexpr uint8_t x_invalidGprOrd = X64Reg::RSP.MachineOrd();

    bool WARN_UNUSED IsAvailableInGPR()
    {
        return BFM_GprLoc::Get(m_compositeVal) != x_invalidGprOrd;
    }

    void InvalidateGPR()
    {
        TestAssert(IsAvailableInGPR());
        BFM_GprLoc::Set(m_compositeVal, x_invalidGprOrd);
    }

    void SetGPR(X64Reg reg)
    {
        TestAssert(!IsAvailableInGPR());
        TestAssert(reg.IsGPR() && reg.MachineOrd() < 16 && reg.MachineOrd() != x_invalidGprOrd);
        BFM_GprLoc::Set(m_compositeVal, reg.MachineOrd());
    }

    bool WARN_UNUSED IsAvailableInFPR()
    {
        return BFM_InFpr::Get(m_compositeVal);
    }

    void InvalidateFPR()
    {
        TestAssert(IsAvailableInFPR());
        BFM_InFpr::Set(m_compositeVal, false);
    }

    void SetFPR(X64Reg reg)
    {
        TestAssert(!IsAvailableInFPR());
        TestAssert(!reg.IsGPR() && reg.MachineOrd() < 32);
        BFM_InFpr::Set(m_compositeVal, true);
        BFM_FprLoc::Set(m_compositeVal, reg.MachineOrd());
    }

    bool WARN_UNUSED IsSpilled() { return m_physicalSpillLoc != static_cast<uint16_t>(-1); }

    uint16_t WARN_UNUSED GetPhysicalSpillSlot() { TestAssert(IsSpilled()); return m_physicalSpillLoc; }

    void SetPhysicalSpillSlot(uint16_t value)
    {
        TestAssert(!IsSpilled() && value != static_cast<uint16_t>(-1));
        m_physicalSpillLoc = value;
    }

    bool WARN_UNUSED HasNextUse() { return m_nextUseInputOrd != static_cast<uint16_t>(-1); }

    // <NodeIdx, InputOrd> identifies the next use of this value
    //
    uint32_t WARN_UNUSED GetNextUseNodeIdx() { TestAssert(HasNextUse()); return BFM_NextUseNodeIdx::Get(m_compositeVal); }
    uint16_t WARN_UNUSED GetNextUseInputOrd() { TestAssert(HasNextUse()); return m_nextUseInputOrd; }

    void SetNoNextUse() { m_nextUseInputOrd = static_cast<uint16_t>(-1); }

    void SetNextUse(uint32_t nodeIdx, uint16_t inputOrd)
    {
        TestAssert(inputOrd != static_cast<uint16_t>(-1));
        BFM_NextUseNodeIdx::Set(m_compositeVal, nodeIdx);
        m_nextUseInputOrd = inputOrd;
    }

private:
    using CarrierTy = uint32_t;

    using BFM_GprLoc = BitFieldMember<CarrierTy, uint8_t, 0 /*start*/, 4 /*width*/>;
    using BFM_InFpr = BitFieldMember<CarrierTy, bool, 4 /*start*/, 1 /*width*/>;
    using BFM_FprLoc = BitFieldMember<CarrierTy, uint8_t, 5 /*start*/, 4 /*width*/>;
    using BFM_NextUseNodeIdx = BitFieldMember<CarrierTy, uint32_t, 9 /*start*/, 23 /*width*/>;

    CarrierTy m_compositeVal;
    uint16_t m_nextUseInputOrd;
    // The physical slot ordinal into the DFG frame where this value is spilled, -1 if not spilled
    // Note that this never points to the register spill area
    //
    uint16_t m_physicalSpillLoc;
};
static_assert(sizeof(ValueRegAllocInfo) == 8);

// Records the reg alloc info of a single use of an SSA value
//
struct alignas(8) ValueUseRAInfo
{
    ValueUseRAInfo()
        : m_nextUseNodeIdx(static_cast<uint32_t>(-1))
        , m_nextUseInputOrd(static_cast<uint16_t>(-1))
        , m_physicalSlot(static_cast<uint16_t>(-1))
    { }

    bool HasNextUse() { return m_nextUseInputOrd != static_cast<uint16_t>(-1); }

    bool ALWAYS_INLINE HasAssignedValidPhysicalSlot()
    {
        return m_physicalSlot != static_cast<uint16_t>(-1);
    }

    size_t ALWAYS_INLINE GetPhysicalSlot()
    {
        TestAssert(HasAssignedValidPhysicalSlot());
        return m_physicalSlot;
    }

    // Where the next use of this SSA value is
    //
    uint32_t m_nextUseNodeIdx;
    uint16_t m_nextUseInputOrd;

    // The physical slot ordinal into the DFG frame where this value sits in
    // If the ordinal points to the reg spill area, it means this value is reg allocated (in the corresponding reg)
    //
    uint16_t m_physicalSlot;
};
static_assert(sizeof(ValueUseRAInfo) == 8);

enum class DfgCodegenFuncOrd : uint16_t;

// [ ValueRegAllocInfo * #outputs ] [ NodeRegAllocInfo ] [ ValueUseRAInfo * #inputs ]
//
struct alignas(8) NodeRegAllocInfo
{
    NodeRegAllocInfo()
        : m_codegenFuncOrd(static_cast<DfgCodegenFuncOrd>(-1))
        , m_rangeOperandPhysicalSlot(static_cast<uint16_t>(-1))
        , m_outputPhysicalSlot(static_cast<uint16_t>(-1))
        , m_brDecisionPhysicalSlot(static_cast<uint16_t>(-1))
#ifdef TESTBUILD
        , m_numInputs(0)
        , m_numOutputs(0)
#endif
    { }

    // Note that currently we do all reg alloc first, and then do codegen.
    //
    // Unlike ValueUseRAInfo (from GetInputRAInfo), which is immutable once reg alloc assigns the physical slot for it,
    // the stuffs in ValueRegAllocInfo gets updated as the reg alloc process proceeds.
    //
    // So at codegen time, this function should not be used,
    // as the returned ValueRegAllocInfo does not contain the information for the current node being generated,
    // but only the end state at the end of reg alloc!
    //
    ValueRegAllocInfo* GetOutputRAInfo(size_t outputOrd)
    {
        TestAssert(outputOrd < m_numOutputs);
        return reinterpret_cast<ValueRegAllocInfo*>(this) - (outputOrd + 1);
    }

    ValueUseRAInfo* ALWAYS_INLINE GetInputRAInfo(size_t inputOrd)
    {
        TestAssert(inputOrd < m_numInputs);
        return m_operandInfo + inputOrd;
    }

    size_t ALWAYS_INLINE GetOutputPhysicalSlot()
    {
        TestAssert(m_outputPhysicalSlot != static_cast<uint16_t>(-1));
        return m_outputPhysicalSlot;
    }

    size_t ALWAYS_INLINE GetBrDecisionPhysicalSlot()
    {
        TestAssert(m_brDecisionPhysicalSlot != static_cast<uint16_t>(-1));
        return m_brDecisionPhysicalSlot;
    }

    size_t ALWAYS_INLINE GetRangeOperandPhysicalSlotStart()
    {
        TestAssert(m_rangeOperandPhysicalSlot != static_cast<uint16_t>(-1));
        return m_rangeOperandPhysicalSlot;
    }

    static NodeRegAllocInfo* WARN_UNUSED Create(TempArenaAllocator& alloc, size_t numInputs, size_t numOutputs)
    {
        // Satisfying alignment requirement for both arrays is tricky in general.. but here alignmment of 8 will do
        //
        static_assert(sizeof(ValueRegAllocInfo) == 8);
        static_assert(alignof(NodeRegAllocInfo) <= 8);
        void* p = alloc.AllocateWithAlignment(8, sizeof(ValueRegAllocInfo) * numOutputs + offsetof_member_v<&NodeRegAllocInfo::m_operandInfo> + sizeof(ValueUseRAInfo) * numInputs);

        Assert(reinterpret_cast<uintptr_t>(p) % alignof(ValueRegAllocInfo) == 0);
        ValueRegAllocInfo* rai = reinterpret_cast<ValueRegAllocInfo*>(p);

        NodeRegAllocInfo* ui = reinterpret_cast<NodeRegAllocInfo*>(rai + numOutputs);
        Assert(reinterpret_cast<uintptr_t>(ui) % alignof(NodeRegAllocInfo) == 0);
        ConstructInPlace(ui);

#ifdef TESTBUILD
        ui->m_numInputs = SafeIntegerCast<uint32_t>(numInputs);
        ui->m_numOutputs = SafeIntegerCast<uint32_t>(numOutputs);
#endif

        for (size_t i = 0; i < numOutputs; i++)
        {
            ConstructInPlace(rai + i);
        }

        for (size_t i = 0; i < numInputs; i++)
        {
            ConstructInPlace(ui->GetInputRAInfo(i));
        }

        return reinterpret_cast<NodeRegAllocInfo*>(ui);
    }

    // The codegen function ordinal that should be called to codegen this node
    //
    DfgCodegenFuncOrd m_codegenFuncOrd;
    // The physical DFG frame location where the range operand starts, if exists
    //
    uint16_t m_rangeOperandPhysicalSlot;
    // The physical DFG frame location for the output and brDecision, if exists
    //
    uint16_t m_outputPhysicalSlot;
    uint16_t m_brDecisionPhysicalSlot;

#ifdef TESTBUILD
    uint32_t m_numInputs;
    uint32_t m_numOutputs;
#endif

    ValueUseRAInfo m_operandInfo[0];
};

}   // namespace dfg
