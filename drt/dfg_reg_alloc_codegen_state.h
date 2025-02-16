#pragma once

#include "common_utils.h"
#include "deegen/deegen_dfg_register_ident_class.h"
#include "dfg_codegen_operation_base.h"
#include "dfg_codegen_protocol.h"
#include "heap_ptr_utils.h"
#include "temp_arena_allocator.h"
#include "x64_register_info.h"
#include "dfg_reg_alloc_register_info.h"
#include "dfg_slowpath_register_config_helper.h"

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
                    val = val * 256 + (list[i].MachineOrd() & 7);
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
                    val = val * 256 + (list[4 + i].MachineOrd() & 7);
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

    void ALWAYS_INLINE PopulateCodegenState(RegAllocStateForCodeGen& cgState /*inout*/)
    {
        TestAssert((m_group1ScratchGprIdxMask & m_group1PassthruGprIdxMask) == 0);
        TestAssert((m_group2ScratchGprIdxMask & m_group2PassthruGprIdxMask) == 0);
        TestAssert((m_scratchFprIdxMask & m_passthruFprIdxMask) == 0);
        uint64_t operandsInfo = UnalignedLoad<uint64_t>(m_operandsInfo);
#ifdef TESTBUILD
        for (size_t i = 0; i < 8; i++)
        {
            uint8_t val = static_cast<uint8_t>((operandsInfo >> (8 * i)) & 255);
            TestAssert(val < 8 || val == RegAllocStateForCodeGen::x_invalidValue);
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
#ifdef TESTBUILD
        for (size_t i = 0; i < cgState.x_stateLength; i++)
        {
            TestAssert(cgState.m_data[i] < 8 || cgState.m_data[i] == RegAllocStateForCodeGen::x_invalidValue);
        }
#endif
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

// See dfg_slowpath_register_config_helper.h
// There's some weird design here since it's the remnant of design changes, but since it works, just leave it as it is.
//
struct RegAllocAllRegPurposeInfo
{
    using RegClass = dast::StencilRegIdentClass;

    struct Item
    {
        RegClass m_regClass;
        uint8_t m_ordInClass;
    };

    // The setup logic must populate <RegClass::X_END_OF_ENUM, 0> to the unused padding entries (>= x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs)
    //
    static constexpr size_t x_length = RoundUpToMultipleOf<DfgSlowPathRegConfigDataTraits::x_packSize>(x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
    Item m_data[x_length];
};

}   // namespace dfg
