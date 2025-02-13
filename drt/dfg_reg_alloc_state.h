#pragma once

#include "common_utils.h"
#include "deegen/deegen_dfg_register_ident_class.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_codegen_protocol.h"
#include "heap_ptr_utils.h"
#include "temp_arena_allocator.h"
#include "x64_register_info.h"
#include "dfg_reg_alloc_register_info.h"

namespace dfg {

// Describes the mapping from abstract register roles (e.g., FPR Scratch #1) to physical registers
//
// This is the direct interface used by the codegen, so it is very low-level.
// Higher-level classes wrap this class to provide better management.
//
// Note that it is always a bug to access invalid entries in the state vector.
// Therefore, the Unset() and IsValid() are no-op in release build: they are only intended
// to catch bugs in test builds.
//
struct RegAllocStateForCodeGen
{
    using RegClass = dast::StencilRegIdentClass;

    RegAllocStateForCodeGen()
    {
#ifdef TESTBUILD
        memset(m_data, x_invalidValue, sizeof(uint8_t) * x_stateLength);
#endif
    }

    static bool WARN_UNUSED IsRegisterCompatibleWithRegClass(X64Reg reg, RegClass regClass)
    {
        TestAssert(regClass != RegClass::X_END_OF_ENUM);
        switch (regClass)
        {
        case RegClass::Operand:
        {
            return true;
        }
        case RegClass::PtNonExtG:
        case RegClass::ScNonExtG:
        {
            return reg.IsGPR() && reg.MachineOrd() < 8;
        }
        case RegClass::PtExtG:
        case RegClass::ScExtG:
        {
            return reg.IsGPR() && reg.MachineOrd() >= 8;
        }
        case RegClass::PtF:
        case RegClass::ScF:
        {
            return !reg.IsGPR() && reg.MachineOrd() < 8;
        }
        default:
        {
            TestAssert(false);
            __builtin_unreachable();
        }
        }   /*switch*/
    }

    size_t GetIdx(RegClass regClass, size_t ordInClass)
    {
        TestAssertImp(regClass < RegClass::X_END_OF_ENUM, ordInClass < 8);
        TestAssertImp(regClass == RegClass::X_END_OF_ENUM, ordInClass == 0);
        size_t idx = static_cast<size_t>(regClass) * 8 + ordInClass;
        TestAssert(idx < x_stateLength);
        return idx;
    }

    uint8_t* GetArrayBaseForClass(RegClass regClass)
    {
        TestAssert(regClass < RegClass::X_END_OF_ENUM);
        size_t idx = static_cast<size_t>(regClass) * 8;
        return m_data + idx;
    }

    void Set(RegClass regClass, size_t ordInClass, uint8_t value)
    {
        TestAssert(value < 8);
#ifdef TESTBUILD
        // Assert that the register machine ordinal is valid for the class
        //
        {
            switch (regClass)
            {
            case RegClass::PtNonExtG:
            case RegClass::ScNonExtG:
            {
                TestAssert(IsRegisterUsedForDfgRegAllocation(X64Reg::GPR(value)));
                break;
            }
            case RegClass::PtExtG:
            case RegClass::ScExtG:
            {
                TestAssert(IsRegisterUsedForDfgRegAllocation(X64Reg::GPR(8 + value)));
                break;
            }
            case RegClass::PtF:
            case RegClass::ScF:
            {
                TestAssert(IsRegisterUsedForDfgRegAllocation(X64Reg::FPR(value)));
                break;
            }
            default:
            {
                break;
            }
            }   /*switch*/
        }
#endif

        size_t idx = GetIdx(regClass, ordInClass);
        TestAssertImp(regClass != RegClass::X_END_OF_ENUM, !IsValid(idx));
        m_data[idx] = value;
    }

    uint8_t Get(RegClass regClass, size_t ordInClass)
    {
        TestAssert(regClass != RegClass::X_END_OF_ENUM);
        size_t idx = GetIdx(regClass, ordInClass);
        TestAssert(IsValid(idx) && idx != x_stateLength - 1);
        TestAssert(m_data[idx] < 8);
        return m_data[idx];
    }

    void Unset([[maybe_unused]] RegClass regClass, [[maybe_unused]] size_t ordInClass)
    {
#ifdef TESTBUILD
        size_t idx = GetIdx(regClass, ordInClass);
        TestAssert(IsValid(idx));
        m_data[idx] = x_invalidValue;
#endif
    }

    void UnsetAllExceptOperands()
    {
#ifdef TESTBUILD
        memset(m_data + 8, x_invalidValue, sizeof(uint8_t) * (x_stateLength - 8));
#endif
    }

    // Only to be used in assertions to check at no invalid accesses are made
    //
#ifdef TESTBUILD
    bool IsValid(size_t idx)
    {
        TestAssert(idx < x_stateLength);
        return m_data[idx] != x_invalidValue;
    }
#endif

    static constexpr size_t x_numRegClasses = static_cast<size_t>(RegClass::X_END_OF_ENUM);
    static constexpr size_t x_stateLength = x_numRegClasses * 8 + 1;

#ifdef TESTBUILD
    static constexpr uint8_t x_invalidValue = 0x7f;
#endif

    // The state vector in the format expected by codegen
    // Logically, it is a 7*8+1 array, where the first dimension is RegClass,
    // and second dimension is ordinal inside the class. The last slot is used as a sentry
    // for unused registers.
    //
    uint8_t m_data[x_stateLength];
};

// Given an array A, and a bitmask M, we want to do get the subsequence of A indexed by the 1-bits in M
//
template<size_t numRegs>
struct RegClassInfoPopulatorHelper
{
    static_assert(numRegs <= 8);

    consteval RegClassInfoPopulatorHelper(std::array<X64Reg, numRegs> list)
    {
        for (uint32_t k = 0; k < x_part1Len; k++)
        {
            uint32_t val = 0x7f7f7f7f;
            for (uint32_t i = 4; i--;)
            {
                if (k & (1U << i))
                {
                    val = val * 256 + list[i].GetCompositeValue();
                }
            }
            m_part1[k] = val;
        }
        for (uint32_t k = 0; k < x_part2Len; k++)
        {
            uint32_t val = 0x7f7f7f7f;
            for (uint32_t i = 4; i--;)
            {
                if (k & (1U << i))
                {
                    val = val * 256 + list[4 + i].GetCompositeValue();
                }
            }
            m_part2[k] = val;
        }
    }

    void ALWAYS_INLINE Populate(uint8_t* dst, uint8_t mask) const
    {
        TestAssert(mask < (1U << numRegs));
        if constexpr(numRegs > 4)
        {
            static_assert(x_part1Len == 16);
            uint8_t k1 = mask & 15;
            UnalignedStore<uint32_t>(dst, m_part1[k1]);

            size_t offset = CountNumberOfOnes(k1);
            uint8_t k2 = mask >> 4;
            TestAssert(k2 < x_part2Len);
            UnalignedStore<uint32_t>(dst + offset, m_part2[k2]);
        }
        else
        {
            TestAssert(mask < x_part1Len);
            UnalignedStore<uint32_t>(dst, m_part1[mask]);
        }
    }

    static constexpr size_t x_part1Len = 1U << (std::min(numRegs, static_cast<size_t>(4)));
    std::array<uint32_t, x_part1Len> m_part1;

    static constexpr size_t x_part2Len = (numRegs <= 4) ? 0 : (1U << (numRegs - 4));
    std::array<uint32_t, x_part2Len> m_part2;
};

inline constexpr auto x_regAllocStateRegConfigPopulatorForGroup1Gpr = RegClassInfoPopulatorHelper<x_dfg_reg_alloc_num_group1_gprs>(
    []() {
        std::array<X64Reg, x_dfg_reg_alloc_num_group1_gprs> res;
        for (size_t i = 0; i < x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            res[i] = x_dfg_reg_alloc_gprs[x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs + i];
            ReleaseAssert(res[i].IsGPR() && res[i].MachineOrd() < 8);
        }
        return res;
    }());

inline constexpr auto x_regAllocStateRegConfigPopulatorForGroup2Gpr = RegClassInfoPopulatorHelper<x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs>(
    []() {
        std::array<X64Reg, x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs> res;
        for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            res[i] = x_dfg_reg_alloc_gprs[i];
            ReleaseAssert(res[i].IsGPR() && res[i].MachineOrd() >= 8);
        }
        return res;
    }());

inline constexpr auto x_regAllocStateRegConfigPopulatorForFpr = RegClassInfoPopulatorHelper<x_dfg_reg_alloc_num_fprs>(
    []() {
        std::array<X64Reg, x_dfg_reg_alloc_num_fprs> res;
        for (size_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
        {
            res[i] = x_dfg_reg_alloc_fprs[i];
            ReleaseAssert(res[i].IsFPR() && res[i].MachineOrd() < 8);
        }
        return res;
    }());

// Describes a register configuration of all relevant registers (operand registers, scratch registers and passthrough registers)
// One can use this struct to construct the RegAllocStateForCodeGen expected by the codegen functions
// This struct lives in the log stream, so must have __packed__
//
struct __attribute__((__packed__)) RegAllocRegConfig
{
    RegAllocRegConfig()
    {
#ifdef TESTBUILD
        memset(m_operandsInfo, RegAllocStateForCodeGen::x_invalidValue, sizeof(uint8_t) * 8);
#endif
    }

    using RegClass = dast::StencilRegIdentClass;

    void SetOperandReg(size_t raOpIdx, X64Reg reg)
    {
        TestAssert(raOpIdx < 8);
        TestAssert(m_operandsInfo[raOpIdx] == RegAllocStateForCodeGen::x_invalidValue);
        m_operandsInfo[raOpIdx] = reg.MachineOrd() & 7;
#ifdef TESTBUILD
        m_operandReg[raOpIdx] = reg;
#endif
    }

    void ALWAYS_INLINE PopulateCodegenState([[maybe_unused]] size_t numOperands, RegAllocStateForCodeGen& cgState /*inout*/)
    {
        TestAssert((m_group1ScratchGprIdxMask & m_group1PassthruGprIdxMask) == 0);
        TestAssert((m_group2ScratchGprIdxMask & m_group2PassthruGprIdxMask) == 0);
        TestAssert((m_scratchFprIdxMask & m_passthruFprIdxMask) == 0);
        uint64_t operandsInfo = UnalignedLoad<uint64_t>(m_operandsInfo);
#ifdef TESTBUILD
        TestAssert(numOperands <= 8);
        for (size_t i = 0; i < 8; i++)
        {
            uint8_t val = static_cast<uint8_t>((operandsInfo >> (8 * i)) & 255);
            if (i < numOperands)
            {
                TestAssert(val < 8 || val == RegAllocStateForCodeGen::x_invalidValue);
            }
            else
            {
                TestAssert(val == RegAllocStateForCodeGen::x_invalidValue);
            }
        }
#endif
        cgState.UnsetAllExceptOperands();
        UnalignedStore<uint64_t>(cgState.m_data, operandsInfo);
        x_regAllocStateRegConfigPopulatorForGroup1Gpr.Populate(cgState.GetArrayBaseForClass(RegClass::ScNonExtG), m_group1ScratchGprIdxMask);
        x_regAllocStateRegConfigPopulatorForGroup2Gpr.Populate(cgState.GetArrayBaseForClass(RegClass::ScExtG), m_group2ScratchGprIdxMask);
        x_regAllocStateRegConfigPopulatorForFpr.Populate(cgState.GetArrayBaseForClass(RegClass::ScF), m_scratchFprIdxMask);
        x_regAllocStateRegConfigPopulatorForGroup1Gpr.Populate(cgState.GetArrayBaseForClass(RegClass::PtNonExtG), m_group1PassthruGprIdxMask);
        x_regAllocStateRegConfigPopulatorForGroup2Gpr.Populate(cgState.GetArrayBaseForClass(RegClass::PtExtG), m_group2PassthruGprIdxMask);
        x_regAllocStateRegConfigPopulatorForFpr.Populate(cgState.GetArrayBaseForClass(RegClass::PtF), m_passthruFprIdxMask);
    }

    void AssertConsistency()
    {
#ifdef TESTBUILD
        uint32_t gprGroup1RegMask = 0;
        uint32_t gprGroup2RegMask = 0;
        uint32_t fprRegMask = 0;
        TestAssert(m_group1ScratchGprIdxMask < (1U << x_dfg_reg_alloc_num_group1_gprs));
        TestAssert(m_group1PassthruGprIdxMask < (1U << x_dfg_reg_alloc_num_group1_gprs));
        for (uint32_t i = 0; i < x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            if (m_group1ScratchGprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_gprs[x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs + i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((gprGroup1RegMask & (1U << ord)) == 0);
                gprGroup1RegMask |= (1U << ord);
            }
            if (m_group1PassthruGprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_gprs[x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs + i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((gprGroup1RegMask & (1U << ord)) == 0);
                gprGroup1RegMask |= (1U << ord);
            }
        }
        TestAssert(m_group2ScratchGprIdxMask < (1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)));
        TestAssert(m_group2PassthruGprIdxMask < (1U << (x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs)));
        for (uint32_t i = 0; i < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            if (m_group2ScratchGprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_gprs[i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((gprGroup2RegMask & (1U << ord)) == 0);
                gprGroup2RegMask |= (1U << ord);
            }
            if (m_group2PassthruGprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_gprs[i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((gprGroup2RegMask & (1U << ord)) == 0);
                gprGroup2RegMask |= (1U << ord);
            }
        }
        TestAssert(m_scratchFprIdxMask < (1U << x_dfg_reg_alloc_num_fprs));
        TestAssert(m_passthruFprIdxMask < (1U << x_dfg_reg_alloc_num_fprs));
        for (uint32_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
        {
            if (m_scratchFprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_fprs[i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((fprRegMask & (1U << ord)) == 0);
                fprRegMask |= (1U << ord);
            }
            if (m_passthruFprIdxMask & (1U << i))
            {
                X64Reg reg = x_dfg_reg_alloc_fprs[i];
                uint32_t ord = reg.MachineOrd() & 7;
                TestAssert((fprRegMask & (1U << ord)) == 0);
                fprRegMask |= (1U << ord);
            }
        }
        for (size_t i = 0; i < 8; i++)
        {
            if (m_operandsInfo[i] != RegAllocStateForCodeGen::x_invalidValue)
            {
                uint32_t ord = m_operandsInfo[i];
                TestAssert(ord == (m_operandReg[i].MachineOrd() & 7));
                if (m_operandReg[i].IsGPR())
                {
                    if (m_operandReg[i].MachineOrd() < 8)
                    {
                        TestAssert((gprGroup1RegMask & (1U << ord)) == 0);
                        gprGroup1RegMask |= (1U << ord);
                    }
                    else
                    {
                        TestAssert((gprGroup2RegMask & (1U << ord)) == 0);
                        gprGroup2RegMask |= (1U << ord);
                    }
                }
                else
                {
                    TestAssert((fprRegMask & (1U << ord)) == 0);
                    fprRegMask |= (1U << ord);
                }
            }
        }
        TestAssert(CountNumberOfOnes(gprGroup1RegMask) == x_dfg_reg_alloc_num_group1_gprs);
        TestAssert(CountNumberOfOnes(gprGroup2RegMask) == x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
        TestAssert(CountNumberOfOnes(fprRegMask) == x_dfg_reg_alloc_num_fprs);
        for (size_t i = 0; i < x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            X64Reg reg = x_dfg_reg_alloc_gprs[x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs + i];
            uint32_t ord = reg.MachineOrd() & 7;
            TestAssert((gprGroup1RegMask & (1U << ord)) > 0);
        }
        for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs; i++)
        {
            X64Reg reg = x_dfg_reg_alloc_gprs[i];
            uint32_t ord = reg.MachineOrd() & 7;
            TestAssert((gprGroup2RegMask & (1U << ord)) > 0);
        }
        for (uint32_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
        {
            X64Reg reg = x_dfg_reg_alloc_fprs[i];
            uint32_t ord = reg.MachineOrd() & 7;
            TestAssert((fprRegMask & (1U << ord)) > 0);
        }
#endif
    }

    uint8_t m_group1ScratchGprIdxMask;
    uint8_t m_group1PassthruGprIdxMask;
    uint8_t m_group2ScratchGprIdxMask;
    uint8_t m_group2PassthruGprIdxMask;
    uint8_t m_scratchFprIdxMask;
    uint8_t m_passthruFprIdxMask;

private:
    // In most cases we won't have 8 operands so it's a bit wasteful, but for now always reserve for maximum for simplicity.
    //
    uint8_t m_operandsInfo[8];

#ifdef TESTBUILD
    // In test build, also track the real register of each operand, so we can assert consistency
    //
    X64Reg m_operandReg[8];
#endif
};

// Information about where each input/output/range operand of the node reside in the stack frame
// If an operand is reg-allocated, the physical slot points to the corresponding slot in the spill area
// This struct lives in the log stream, so must have __packed__
//
struct __attribute__((__packed__)) NodeOperandConfigData
{
    NodeOperandConfigData()
        : m_codegenFuncOrd(static_cast<DfgCodegenFuncOrd>(-1))
        , m_rangeOperandPhysicalSlot(static_cast<uint16_t>(-1))
        , m_outputPhysicalSlot(static_cast<uint16_t>(-1))
        , m_brDecisionPhysicalSlot(static_cast<uint16_t>(-1))
        , m_numInputOperands(static_cast<uint8_t>(-1))
    { }

    DfgCodegenFuncOrd ALWAYS_INLINE GetCodegenFuncOrd()
    {
        TestAssert(m_codegenFuncOrd != static_cast<DfgCodegenFuncOrd>(-1));
        return m_codegenFuncOrd;
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

    size_t ALWAYS_INLINE GetInputOperandPhysicalSlot(size_t inputOrd)
    {
        TestAssert(m_numInputOperands != static_cast<uint8_t>(-1));
        TestAssert(inputOrd < m_numInputOperands);
        return m_inputOperandPhysicalSlots[inputOrd];
    }

    static constexpr size_t TrailingArrayOffset()
    {
        return offsetof_member_v<&NodeOperandConfigData::m_inputOperandPhysicalSlots>;
    }

    static constexpr size_t GetAllocationSize(size_t numInputOperands)
    {
        return TrailingArrayOffset() + sizeof(uint16_t) * numInputOperands;
    }

    void* WARN_UNUSED GetStructEnd()
    {
        TestAssert(m_numInputOperands != static_cast<uint8_t>(-1));
        return reinterpret_cast<uint8_t*>(this) + GetAllocationSize(m_numInputOperands);
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
    uint8_t m_numInputOperands;

    uint16_t m_inputOperandPhysicalSlots[0];
};

struct RegAllocCodegenStateLogItem
{
    using RegClass = dast::StencilRegIdentClass;

    bool IsSentinel() { return m_idx == static_cast<uint8_t>(-1); }

    static RegAllocCodegenStateLogItem WARN_UNUSED Sentinel()
    {
        return { .m_idx = static_cast<uint8_t>(-1), .m_value = static_cast<uint8_t>(-1) };
    }

    void Execute(RegAllocStateForCodeGen& state)
    {
        TestAssert(!IsSentinel());

        RegClass regClass = static_cast<RegClass>(m_idx >> 3);
        TestAssert(regClass < RegClass::X_END_OF_ENUM);
        size_t ordInClass = m_idx & 7;

#ifdef TESTBUILD
        if (m_value == RegAllocStateForCodeGen::x_invalidValue)
        {
            state.Unset(regClass, ordInClass);
            return;
        }
#endif

        TestAssert(m_value < 8);
        state.Set(regClass, ordInClass, m_value);
    }

    static RegAllocCodegenStateLogItem WARN_UNUSED Create(RegClass regClass, size_t ordInClass, uint8_t value)
    {
        TestAssert(regClass < RegClass::X_END_OF_ENUM && ordInClass < 8);
        uint8_t idx = static_cast<uint8_t>(static_cast<size_t>(regClass) * 8 + ordInClass);
        TestAssert(idx < RegAllocStateForCodeGen::x_stateLength);
        TestAssert(value < 8 || value == RegAllocStateForCodeGen::x_invalidValue);
        return { .m_idx = idx, .m_value = value };
    }

    uint8_t m_idx;
    uint8_t m_value;
};

struct RegAllocCodegenStateLogReplayer
{
    RegAllocCodegenStateLogReplayer()
        : m_curItem(nullptr), m_state(nullptr), m_log(nullptr), m_logEnd(nullptr)
    { }

    RegAllocCodegenStateLogReplayer(RegAllocCodegenStateLogItem* log, size_t numItems)
        : m_curItem(nullptr), m_state(nullptr), m_log(log), m_logEnd(log + numItems)
    { }

    void ResetAndAttachState(RegAllocStateForCodeGen* state)
    {
        TestAssert(state != nullptr);
        m_state = state;
        m_curItem = m_log;
    }

    bool HasMore() { TestAssert(m_state != nullptr && m_curItem <= m_logEnd); return m_curItem < m_logEnd; }

    void ReplayToNextSentinelPoint()
    {
        TestAssert(HasMore());
        while (!m_curItem->IsSentinel())
        {
            m_curItem->Execute(*m_state);
            m_curItem++;
            TestAssert(m_curItem < m_logEnd);
        }
        m_curItem++;
    }

    RegAllocStateForCodeGen* GetState() { return m_state; }

private:
    RegAllocCodegenStateLogItem* m_curItem;
    RegAllocStateForCodeGen* m_state;
    RegAllocCodegenStateLogItem* m_log;
    RegAllocCodegenStateLogItem* m_logEnd;
};

// Additionally logs every action to RegAllocStateForCodeGen so we can replay it later
//
struct RegAllocCodegenStateWithLog
{
    using RegClass = dast::StencilRegIdentClass;

    // This class takes an external log buffer, but does not own it.
    // This allows the same log buffer to be reused for multiple instances to reduce vector resize cost.
    //
    // When the log is fully built, it is copied out using an exact memory allocation.
    //
    RegAllocCodegenStateWithLog(TempVector<RegAllocCodegenStateLogItem>& logBuffer)
        : m_log(logBuffer)
#ifdef TESTBUILD
        , m_logTaken(false)
#endif
    { }

    // This copies the log (using 'alloc'), so the log buffer can be cleared and reused later.
    //
    RegAllocCodegenStateLogReplayer WARN_UNUSED GetLog(TempArenaAllocator& alloc)
    {
        TestAssert(!m_logTaken);
#ifdef TESTBUILD
        m_logTaken = true;
#endif
        // All the log past the last sentinel are not useful since they can never be validly replayed (we only allow replaying to a sentinel point)
        //
        size_t usefulLogSize = m_log.size();
        while (usefulLogSize > 0 && !m_log[usefulLogSize - 1].IsSentinel())
        {
            usefulLogSize--;
        }
        RegAllocCodegenStateLogItem* log = alloc.AllocateArray<RegAllocCodegenStateLogItem>(usefulLogSize);
        memcpy(log, m_log.data(), sizeof(RegAllocCodegenStateLogItem) * usefulLogSize);
        return RegAllocCodegenStateLogReplayer(log, usefulLogSize);
    }

    void Set(RegClass regClass, size_t ordInClass, uint8_t value)
    {
        TestAssert(!m_logTaken);
        m_state.Set(regClass, ordInClass, value);
        m_log.push_back(RegAllocCodegenStateLogItem::Create(regClass, ordInClass, value));
    }

    void Unset([[maybe_unused]] RegClass regClass, [[maybe_unused]] size_t ordInClass)
    {
#ifdef TESTBUILD
        TestAssert(!m_logTaken);
        m_state.Unset(regClass, ordInClass);
        m_log.push_back(RegAllocCodegenStateLogItem::Create(regClass, ordInClass, RegAllocStateForCodeGen::x_invalidValue));
#endif
    }

    uint8_t WARN_UNUSED Get(RegClass regClass, size_t ordInClass)
    {
        return m_state.Get(regClass, ordInClass);
    }

    void LogSentinel()
    {
        TestAssert(!m_logTaken);
        m_log.push_back(RegAllocCodegenStateLogItem::Sentinel());
    }

    RegAllocStateForCodeGen m_state;
    TempVector<RegAllocCodegenStateLogItem>& m_log;
#ifdef TESTBUILD
    bool m_logTaken;
#endif
};

// Tracks the allocation state of each register participating in reg alloc
//
struct DfgRegAllocState
{
    using RegClass = dast::StencilRegIdentClass;

    static constexpr size_t x_numRegClasses = RegAllocStateForCodeGen::x_numRegClasses;

    RegAllocStateForCodeGen& GetStateForCodeGen() { return m_cgState.m_state; }

    size_t WARN_UNUSED NumFreeGPRs()
    {
        return GetRegClassSize(RegClass::ScNonExtG) + GetRegClassSize(RegClass::ScExtG);
    }

    size_t WARN_UNUSED NumFreeFPRs() { return GetRegClassSize(RegClass::ScF); }

    size_t WARN_UNUSED NumActiveGPRs()
    {
        return GetRegClassSize(RegClass::PtNonExtG) + GetRegClassSize(RegClass::PtExtG);
    }

    size_t WARN_UNUSED NumActiveGroup1GPRs() { return GetRegClassSize(RegClass::PtNonExtG); }
    size_t WARN_UNUSED NumActiveFPRs() { return GetRegClassSize(RegClass::PtF); }

    // Get any free GPR, use it for operand 'operandOrd'.
    // Return that GPR.
    //
    X64Reg WARN_UNUSED UseFreeGprAsOperand(uint8_t operandOrd)
    {
        X64Reg reg;
        if (GetRegClassSize(RegClass::ScNonExtG) > 0)
        {
            reg = PopFreeRegisterFromClass<RegClass::ScNonExtG>();
        }
        else
        {
            TestAssert(GetRegClassSize(RegClass::ScExtG) > 0);
            reg = PopFreeRegisterFromClass<RegClass::ScExtG>();
        }
        SetPoppedRegisterAsOperand(reg, operandOrd);
        return reg;
    }

    // See above, but for FPR.
    //
    X64Reg WARN_UNUSED UseFreeFprAsOperand(uint8_t operandOrd)
    {
        TestAssert(NumFreeFPRs() > 0);
        X64Reg reg = PopFreeRegisterFromClass<RegClass::ScF>();
        SetPoppedRegisterAsOperand(reg, operandOrd);
        return reg;
    }

    // Use the specified active (state = PassThru) register for operand 'operandOrd'.
    //
    void UseActiveRegAsOperand(X64Reg reg, uint8_t operandOrd)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        PopActiveRegister(reg);
        SetPoppedRegisterAsOperand(reg, operandOrd);
    }

    // Set an active (state = PassThru) register free (state => Scratch).
    //
    void FreeRegister(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        AssertRegisterIsActive(reg);
        PopActiveRegister(reg);
        SetPoppedRegisterAsFree(reg);
    }

    void SetFreeRegisterAsActive(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        AssertRegisterIsFree(reg);
        PopFreeRegister(reg);
        SetPoppedRegisterAsActive(reg);
    }

    void AssertRegisterIsActive([[maybe_unused]] X64Reg reg)
    {
#ifdef TESTBUILD
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetPassthruRegClassForReg(reg);
        AssertValueExistsInClass(regClass, reg.MachineOrd() & 7);
#endif
    }

    void AssertRegisterIsFree([[maybe_unused]] X64Reg reg)
    {
#ifdef TESTBUILD
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetScratchRegClassForReg(reg);
        AssertValueExistsInClass(regClass, reg.MachineOrd() & 7);
#endif
    }

    // The codegen function expects an exact number of passthroughs
    // If we have more passthroughs than allowed, obviously they need to be spilled.
    // But if we have fewer needed passthroughs, we need to turn some scratch registers
    // into dummy passthroughs to make the codegen function happy
    //
    struct FillDummyPassthroughRecord
    {
        FillDummyPassthroughRecord()
            : m_numGprGroup1ToUndo(0)
            , m_numGprGroup2ToUndo(0)
            , m_numFprToUndo(0)
        { }

        size_t m_numGprGroup1ToUndo;
        size_t m_numGprGroup2ToUndo;
        size_t m_numFprToUndo;
    };

    // This function should only be called right before codegen
    // This implies that the number of GPR/FPR passthroughs may be smaller than desired (which this function fixes), but never larger than desired
    //
    // Note that we intentionally do not invalidate the Scratch registers in CodegenState, as they will be immediately restored after codegen.
    // This is a bit hacky, but this operation needs to be done before each codegen and we don't want to generate too much log..
    //
    FillDummyPassthroughRecord WARN_UNUSED FillDummyPassthroughsBeforeCodegen(size_t numGPRs, size_t numFPRs)
    {
        FillDummyPassthroughRecord res;
        {
            size_t numPtGroup1Gprs = GetRegClassSize(RegClass::PtNonExtG);
            size_t numPtGroup2Gprs = GetRegClassSize(RegClass::PtExtG);
            TestAssert(numPtGroup1Gprs + numPtGroup2Gprs <= numGPRs);
            if (numPtGroup1Gprs + numPtGroup2Gprs < numGPRs)
            {
                size_t needed = numGPRs - numPtGroup1Gprs - numPtGroup2Gprs;
                size_t numFreeGroup2GPRs = GetRegClassSize(RegClass::ScExtG);
                if (numFreeGroup2GPRs >= needed)
                {
                    res.m_numGprGroup2ToUndo = needed;
                    SetDummyPassthruFromScratch<RegClass::ScExtG>(needed);
                }
                else
                {
                    size_t numGroup1GprToRemove = needed - numFreeGroup2GPRs;
                    TestAssert(GetRegClassSize(RegClass::ScNonExtG) >= numGroup1GprToRemove);
                    res.m_numGprGroup1ToUndo = numGroup1GprToRemove;
                    SetDummyPassthruFromScratch<RegClass::ScNonExtG>(numGroup1GprToRemove);
                    res.m_numGprGroup2ToUndo = numFreeGroup2GPRs;
                    SetDummyPassthruFromScratch<RegClass::ScExtG>(numFreeGroup2GPRs);
                }
            }
        }
        {
            size_t numPtFprs = GetRegClassSize(RegClass::PtF);
            TestAssert(numPtFprs <= numFPRs);
            if (numPtFprs < numFPRs)
            {
                size_t needed = numFPRs - numPtFprs;
                TestAssert(GetRegClassSize(RegClass::ScF) >= needed);
                res.m_numFprToUndo = needed;
                SetDummyPassthruFromScratch<RegClass::ScF>(needed);
            }
        }
        TestAssert(GetRegClassSize(RegClass::PtNonExtG) + GetRegClassSize(RegClass::PtExtG) == numGPRs);
        TestAssert(GetRegClassSize(RegClass::PtF) == numFPRs);
        return res;
    }

    // Put all registers currently used as operands to the active register pool (state => PassThru) after code generation
    //
    void ClearOperands(FillDummyPassthroughRecord recToUndo)
    {
        // Must undo the FillDummyPassthroughRecord *before* clearing operands!
        //
        UndoSetDummyPassthruFromScratch<RegClass::PtNonExtG>(recToUndo.m_numGprGroup1ToUndo);
        UndoSetDummyPassthruFromScratch<RegClass::PtExtG>(recToUndo.m_numGprGroup2ToUndo);
        UndoSetDummyPassthruFromScratch<RegClass::PtF>(recToUndo.m_numFprToUndo);

        // Move all operands back to the corresponding passthru class
        //
        uint8_t tmp = m_operandValidMask;
        while (tmp != 0)
        {
            size_t ordinal = CountTrailingZeros(tmp);
            tmp ^= static_cast<uint8_t>(1 << ordinal);
            X64Reg reg = m_operands[ordinal];
            TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
            TestAssert(m_cgState.Get(RegClass::Operand, ordinal) == (reg.MachineOrd() & 7));
            m_cgState.Unset(RegClass::Operand, ordinal);
            SetPoppedRegisterAsActive(reg);
        }
        m_operandValidMask = 0;

        TestAssert(GetRegClassSize(RegClass::PtNonExtG) + GetRegClassSize(RegClass::ScNonExtG) == x_dfg_reg_alloc_num_group1_gprs);
        TestAssert(GetRegClassSize(RegClass::PtExtG) + GetRegClassSize(RegClass::ScExtG) == x_dfg_reg_alloc_num_gprs - x_dfg_reg_alloc_num_group1_gprs);
        TestAssert(GetRegClassSize(RegClass::PtF) + GetRegClassSize(RegClass::ScF) == x_dfg_reg_alloc_num_fprs);
    }

    size_t GetRegClassSize(RegClass regClass)
    {
        TestAssert(regClass != RegClass::Operand && regClass != RegClass::X_END_OF_ENUM);
        return m_classSize[static_cast<size_t>(regClass)];
    }

    void AssertConsistency([[maybe_unused]] bool alsoCheckInvalidity = true)
    {
#ifdef TESTBUILD
        uint64_t showedUpRegMask = 0;
        for (size_t i = 0; i < x_numRegClasses; i++)
        {
            RegClass regClass = static_cast<RegClass>(i);
            if (regClass == RegClass::Operand) { continue; }
            size_t num = GetRegClassSize(regClass);
            TestAssert(num < 8);
            for (size_t ord = 0; ord < 8; ord++)
            {
                if (ord < num)
                {
                    size_t regSubOrdinal = m_cgState.Get(regClass, ord);
                    X64Reg reg = GetRegisterFromClassAndRegSubOrdinal(regClass, regSubOrdinal);
                    size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                    TestAssert((showedUpRegMask & (static_cast<uint64_t>(1) << seqOrd)) == 0);
                    showedUpRegMask |= static_cast<uint64_t>(1) << seqOrd;
                }
                else if (alsoCheckInvalidity)
                {
                    TestAssert(!GetStateForCodeGen().IsValid(GetStateForCodeGen().GetIdx(regClass, ord)));
                }
            }
        }

        for (size_t ord = 0; ord < 8; ord++)
        {
            if ((m_operandValidMask & static_cast<uint8_t>(1 << ord)) != 0)
            {
                X64Reg reg = m_operands[ord];
                size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                TestAssert((showedUpRegMask & (static_cast<uint64_t>(1) << seqOrd)) == 0);
                showedUpRegMask |= static_cast<uint64_t>(1) << seqOrd;
                size_t regSubOrdinal = m_cgState.Get(RegClass::Operand, ord);
                TestAssert(regSubOrdinal == (reg.MachineOrd() & 7));
            }
            else
            {
                TestAssert(!GetStateForCodeGen().IsValid(GetStateForCodeGen().GetIdx(RegClass::Operand, ord)));
            }
        }

        TestAssert(showedUpRegMask == (static_cast<uint64_t>(1) << (x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs)) - 1);
#endif
    }

    DfgRegAllocState(TempVector<RegAllocCodegenStateLogItem>& logBuffer)
        : m_cgState(logBuffer)
    {
        m_operandValidMask = 0;
        for (size_t i = 0; i < x_numRegClasses; i++)
        {
            m_classSize[i] = 0;
        }
        ForEachDfgRegAllocRegister([&](X64Reg reg) ALWAYS_INLINE {
            SetPoppedRegisterAsFree(reg);
        });
        AssertConsistency();
    }

    struct AllRegPurposeInfo
    {
        static constexpr size_t x_length = RoundUpToMultipleOf<5>(x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
        std::pair<RegClass, uint8_t /*ordInClass*/> m_data[x_length];
    };

    AllRegPurposeInfo WARN_UNUSED GetAllRegPurposeInfo()
    {
        AllRegPurposeInfo res;
#ifdef TESTBUILD
        uint64_t populatedMask = 0;
#endif
        {
            uint8_t tmp = m_operandValidMask;
            while (tmp != 0)
            {
                size_t ordinal = CountTrailingZeros(tmp);
                tmp ^= static_cast<uint8_t>(1 << ordinal);
                X64Reg reg = m_operands[ordinal];
                TestAssert(m_cgState.Get(RegClass::Operand, ordinal) == (reg.MachineOrd() & 7));
                size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                res.m_data[seqOrd] = std::make_pair(RegClass::Operand, ordinal);
#ifdef TESTBUILD
                TestAssert((populatedMask & (static_cast<uint64_t>(1) << seqOrd)) == 0);
                populatedMask |= static_cast<uint64_t>(1) << seqOrd;
#endif
            }
        }

        auto workForClass = [&]<RegClass regClass>() ALWAYS_INLINE
        {
            size_t size = GetRegClassSize(regClass);
            for (size_t ord = 0; ord < size; ord++)
            {
                size_t regSubOrdinal = m_cgState.Get(regClass, ord);
                X64Reg reg = GetRegisterFromClassAndRegSubOrdinal(regClass, regSubOrdinal);
                size_t seqOrd = GetDfgRegAllocSequenceOrdForReg(reg);
                res.m_data[seqOrd] = std::make_pair(regClass, ord);
#ifdef TESTBUILD
                TestAssert((populatedMask & (static_cast<uint64_t>(1) << seqOrd)) == 0);
                populatedMask |= static_cast<uint64_t>(1) << seqOrd;
#endif
            }
        };

        workForClass.operator()<RegClass::ScNonExtG>();
        workForClass.operator()<RegClass::ScExtG>();
        workForClass.operator()<RegClass::ScF>();
        workForClass.operator()<RegClass::PtNonExtG>();
        workForClass.operator()<RegClass::PtExtG>();
        workForClass.operator()<RegClass::PtF>();

        TestAssert(populatedMask == (static_cast<uint64_t>(1) << (x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs)) - 1);

        for (size_t idx = x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs; idx < AllRegPurposeInfo::x_length; idx++)
        {
            res.m_data[idx] = std::make_pair(RegClass::X_END_OF_ENUM, 0);
        }
        return res;
    }

    RegAllocCodegenStateLogReplayer WARN_UNUSED GetRegStateReplayLog(TempArenaAllocator& alloc)
    {
        return m_cgState.GetLog(alloc);
    }

    void LogSentinelEvent()
    {
        m_cgState.LogSentinel();
    }

private:
    X64Reg ALWAYS_INLINE GetRegisterFromClassAndRegSubOrdinal(RegClass regClass, size_t regSubOrdinal)
    {
        TestAssert(regSubOrdinal < 8);
        __builtin_assume(regSubOrdinal < 8);
        X64Reg reg;
        switch (regClass)
        {
        case RegClass::ScNonExtG:
        case RegClass::PtNonExtG:
        {
            reg = X64Reg::GPR(regSubOrdinal);
            break;
        }
        case RegClass::ScExtG:
        case RegClass::PtExtG:
        {
            reg = X64Reg::GPR(8 + regSubOrdinal);
            break;
        }
        case RegClass::ScF:
        case RegClass::PtF:
        {
            reg = X64Reg::FPR(regSubOrdinal);
            break;
        }
        default:
        {
            TestAssert(false);
            __builtin_unreachable();
        }
        }   /*switch*/
        return reg;
    }

    static RegClass WARN_UNUSED GetScratchRegClassForReg(X64Reg reg)
    {
        if (reg.IsGPR())
        {
            return (reg.MachineOrd() < 8) ? RegClass::ScNonExtG : RegClass::ScExtG;
        }
        else
        {
            TestAssert(reg.MachineOrd() < 8);
            return RegClass::ScF;
        }
    }

    static RegClass WARN_UNUSED GetPassthruRegClassForReg(X64Reg reg)
    {
        if (reg.IsGPR())
        {
            return (reg.MachineOrd() < 8) ? RegClass::PtNonExtG : RegClass::PtExtG;
        }
        else
        {
            TestAssert(reg.MachineOrd() < 8);
            return RegClass::PtF;
        }
    }

    // Return the Passthru class from the Scratch class, or the Scratch class from the Passthru class
    //
    static constexpr RegClass WARN_UNUSED GetOppositeClass(RegClass rc)
    {
        switch (rc)
        {
        case RegClass::ScNonExtG: { return RegClass::PtNonExtG; }
        case RegClass::ScExtG: { return RegClass::PtExtG; }
        case RegClass::ScF: { return RegClass::PtF; }
        case RegClass::PtNonExtG: { return RegClass::ScNonExtG; }
        case RegClass::PtExtG: { return RegClass::ScExtG; }
        case RegClass::PtF: { return RegClass::ScF; }
        default: { TestAssert(false); __builtin_unreachable(); }
        }   /*switch*/
    }

    // Only intended to be used by FillDummyPassthruForCodegen
    // We intentionally do not invalidate the slots in the scratch class, see comments in FillDummyPassthroughsBeforeCodegen
    //
    template<RegClass scRegClass>
    void ALWAYS_INLINE SetDummyPassthruFromScratch(size_t num)
    {
        static_assert(scRegClass == RegClass::ScNonExtG || scRegClass == RegClass::ScExtG || scRegClass == RegClass::ScF);
        size_t scSize = GetRegClassSize(scRegClass);
        TestAssert(scSize >= num);
        size_t scStartOrd = scSize - num;
        constexpr RegClass dstClass = GetOppositeClass(scRegClass);
        size_t dstSize = GetRegClassSize(dstClass);
        TestAssert(dstSize + num < 8);
        for (size_t i = 0; i < num; i++)
        {
            m_cgState.Set(dstClass, dstSize + i, m_cgState.Get(scRegClass, scStartOrd + i));
        }
        m_classSize[static_cast<size_t>(scRegClass)] -= num;
        m_classSize[static_cast<size_t>(dstClass)] += num;
    }

    // Undo the changes of SetDummyPassthruFromScratch
    //
    template<RegClass ptRegClass>
    void ALWAYS_INLINE UndoSetDummyPassthruFromScratch(size_t num)
    {
        static_assert(ptRegClass == RegClass::PtNonExtG || ptRegClass == RegClass::PtExtG || ptRegClass == RegClass::PtF);
#ifdef TESTBUILD
        size_t size = GetRegClassSize(ptRegClass);
        TestAssert(size >= num);
        for (size_t i = 0; i < num; i++)
        {
            m_cgState.Unset(ptRegClass, size - num + i);
        }
#endif
        constexpr RegClass scClass = GetOppositeClass(ptRegClass);
        TestAssert(GetRegClassSize(scClass) + num <= 8);
        m_classSize[static_cast<size_t>(ptRegClass)] -= num;
        m_classSize[static_cast<size_t>(scClass)] += num;
    }

    template<RegClass regClass>
    X64Reg WARN_UNUSED PopFreeRegisterFromClass()
    {
        size_t size = GetRegClassSize(regClass);
        TestAssert(size > 0);
        uint8_t regSubOrdinal = m_cgState.Get(regClass, size - 1);
        m_cgState.Unset(regClass, size - 1);
        m_classSize[static_cast<size_t>(regClass)]--;

        X64Reg reg = GetRegisterFromClassAndRegSubOrdinal(regClass, regSubOrdinal);
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        return reg;
    }

    void PopActiveRegister(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetPassthruRegClassForReg(reg);
        size_t ordInClass = FindValueInClass(regClass, reg.MachineOrd() & 7);
        TestAssert(m_cgState.Get(regClass, ordInClass) == (reg.MachineOrd() & 7));
        m_cgState.Unset(regClass, ordInClass);
        size_t size = GetRegClassSize(regClass);
        TestAssert(ordInClass < size);
        if (ordInClass + 1 < size)
        {
            m_cgState.Set(regClass, ordInClass, m_cgState.Get(regClass, size - 1));
            m_cgState.Unset(regClass, size - 1);
        }
        m_classSize[static_cast<size_t>(regClass)]--;
    }

    void PopFreeRegister(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetScratchRegClassForReg(reg);
        size_t ordInClass = FindValueInClass(regClass, reg.MachineOrd() & 7);
        TestAssert(m_cgState.Get(regClass, ordInClass) == (reg.MachineOrd() & 7));
        m_cgState.Unset(regClass, ordInClass);
        size_t size = GetRegClassSize(regClass);
        TestAssert(ordInClass < size);
        if (ordInClass + 1 < size)
        {
            m_cgState.Set(regClass, ordInClass, m_cgState.Get(regClass, size - 1));
            m_cgState.Unset(regClass, size - 1);
        }
        m_classSize[static_cast<size_t>(regClass)]--;
    }

    void SetPoppedRegisterAsFree(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetScratchRegClassForReg(reg);
        size_t size = GetRegClassSize(regClass);
        TestAssert(size < 8);
        m_cgState.Set(regClass, size, reg.MachineOrd() & 7);
        m_classSize[static_cast<size_t>(regClass)]++;
    }

    void SetPoppedRegisterAsActive(X64Reg reg)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        RegClass regClass = GetPassthruRegClassForReg(reg);
        size_t size = GetRegClassSize(regClass);
        TestAssert(size < 8);
        m_cgState.Set(regClass, size, reg.MachineOrd() & 7);
        m_classSize[static_cast<size_t>(regClass)]++;
    }

    void SetPoppedRegisterAsOperand(X64Reg reg, uint8_t operandOrd)
    {
        TestAssert(IsRegisterUsedForDfgRegAllocation(reg));
        TestAssert(operandOrd < 8);
        TestAssert((m_operandValidMask & static_cast<uint8_t>(1 << operandOrd)) == 0);
        m_operandValidMask |= static_cast<uint8_t>(1 << operandOrd);
        m_operands[operandOrd] = reg;
        m_cgState.Set(RegClass::Operand, operandOrd, reg.MachineOrd() & 7);
    }

    void AssertValueExistsInClass([[maybe_unused]] RegClass regClass, [[maybe_unused]] uint8_t regSubOrdinal)
    {
#ifdef TESTBUILD
        TestAssert(regClass != RegClass::Operand && regClass != RegClass::X_END_OF_ENUM);
        TestAssert(regSubOrdinal < 8);
        size_t size = GetRegClassSize(regClass);
        uint8_t* base = GetStateForCodeGen().m_data + static_cast<size_t>(regClass) * 8;
        bool found = false;
        for (size_t i = 0; i < size; i++)
        {
            if (base[i] == regSubOrdinal)
            {
                TestAssert(!found);
                found = true;
            }
        }
        TestAssert(found);
#endif
    }

    // It is a bug to try to find a non-existent value!
    //
    size_t WARN_UNUSED ALWAYS_INLINE FindValueInClass(RegClass regClass, uint8_t regSubOrdinal)
    {
        AssertValueExistsInClass(regClass, regSubOrdinal);
        TestAssert(regClass != RegClass::Operand && regClass != RegClass::X_END_OF_ENUM);
        TestAssert(regSubOrdinal < 8);
        uint64_t val64 = UnalignedLoad<uint64_t>(GetStateForCodeGen().m_data + static_cast<size_t>(regClass) * 8);

        // An alternate (and interesting) way of doing this is to take advantage of the fact that all regSubOrdinals <= 127:
        //     uint64_t eqMask = ((val64 ^ (regSubOrdinal * 0x101010101010101ULL)) - 0x101010101010101ULL) & 0x8080808080808080ULL;
        //     size_t offset = CountTrailingZeros(eqMask) >> 3;
        //
        // uiCA believes that the above version runs slightly faster (if optimal assembly is generated) than the SSE version below,
        // but LLVM has issue handling large constants and fails to generate the optimal assembly..
        // Anyway, the perf difference is negligible, so just use the SSE version..
        //
        __m128i value = _mm_set_epi64x(0 /*upper*/, static_cast<long long>(val64));
        __m128i mask = _mm_set_epi64x(0 /*upper*/, static_cast<long long>(regSubOrdinal * 0x101010101010101ULL));
        __m128i eqMask = _mm_cmpeq_epi8(value, mask);
        int x = _mm_movemask_epi8(eqMask);
        TestAssert(x != 0);
        size_t offset = CountTrailingZeros(static_cast<unsigned int>(x));
        TestAssert(offset < GetRegClassSize(regClass));
        TestAssert(m_cgState.Get(regClass, offset) == regSubOrdinal);
        return offset;
    }

    // The bitmask of the operand ordinals currently in use
    //
    uint8_t m_operandValidMask;
    // The register for each operand ordinal
    //
    X64Reg m_operands[8];
    // The current total number of registers in each regClass (except Operand class)
    //
    uint8_t m_classSize[x_numRegClasses];
    RegAllocCodegenStateWithLog m_cgState;
};

}   // namespace dfg
