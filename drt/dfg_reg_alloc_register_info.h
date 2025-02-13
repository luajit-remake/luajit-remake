#pragma once

#include "common_utils.h"
#include "x64_register_info.h"

// The general-purpose registers that participate in DFG register allocation
// Group 1 (the first eight) registers should come after Group 2 (r8-15) registers
// The register allocator will prefer using registers showing up earlier in this list
//
inline constexpr X64Reg x_dfg_reg_alloc_gprs[] = {
    X64Reg::R10,
    X64Reg::R11,
    X64Reg::R14,
    X64Reg::R9,
    X64Reg::R8,
    X64Reg::RSI,
    X64Reg::RDI
};

// The floating-point registers that participate in DFG register allocation
//
inline constexpr X64Reg x_dfg_reg_alloc_fprs[] = {
    X64Reg::XMM1,
    X64Reg::XMM2,
    X64Reg::XMM3,
    X64Reg::XMM4,
    X64Reg::XMM5,
    X64Reg::XMM6
};

// This register is not used for reg alloc (so generating anything except hardcoded assembly may garbage it),
// but it is used as a temp reg that serves custom purposes.
// Specifically, constants can be materialized into this reg (which can then be spilled without impacting reg alloc state),
// hardcoded assembly (spills and loads) may use this reg, and this reg is expected by the OSR exit handler to indicate the point of OSR exit
//
constexpr X64Reg x_dfg_custom_purpose_temp_reg = X64Reg::RDX;

constexpr size_t x_dfg_reg_alloc_num_gprs = std::extent_v<decltype(x_dfg_reg_alloc_gprs)>;
constexpr size_t x_dfg_reg_alloc_num_fprs = std::extent_v<decltype(x_dfg_reg_alloc_fprs)>;

template<typename ActionFunc>
void ALWAYS_INLINE ForEachDfgRegAllocGPR(const ActionFunc& action)
{
    for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs; i++)
    {
        action(x_dfg_reg_alloc_gprs[i]);
    }
}

template<typename ActionFunc>
void ALWAYS_INLINE ForEachDfgRegAllocFPR(const ActionFunc& action)
{
    for (size_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
    {
        action(x_dfg_reg_alloc_fprs[i]);
    }
}

template<typename ActionFunc>
void ALWAYS_INLINE ForEachDfgRegAllocRegister(const ActionFunc& action)
{
    ForEachDfgRegAllocGPR(action);
    ForEachDfgRegAllocFPR(action);
}

namespace internal {

inline constexpr std::array<uint8_t, X64Reg::x_totalNumGprs> x_gpr_to_seq_ord_map = []() {
    std::array<uint8_t, X64Reg::x_totalNumGprs> result;
    for (size_t i = 0; i < X64Reg::x_totalNumGprs; i++) { result[i] = static_cast<uint8_t>(-1); }
    for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs; i++)
    {
        ReleaseAssert(result[x_dfg_reg_alloc_gprs[i].MachineOrd()] == static_cast<uint8_t>(-1));
        result[x_dfg_reg_alloc_gprs[i].MachineOrd()] = static_cast<uint8_t>(i);
    }
    return result;
}();

inline constexpr std::array<uint8_t, X64Reg::x_totalNumFprs> x_fpr_to_seq_ord_map = []() {
    std::array<uint8_t, X64Reg::x_totalNumFprs> result;
    for (size_t i = 0; i < X64Reg::x_totalNumFprs; i++) { result[i] = static_cast<uint8_t>(-1); }
    for (size_t i = 0; i < x_dfg_reg_alloc_num_fprs; i++)
    {
        ReleaseAssert(result[x_dfg_reg_alloc_fprs[i].MachineOrd()] == static_cast<uint8_t>(-1));
        result[x_dfg_reg_alloc_fprs[i].MachineOrd()] = static_cast<uint8_t>(x_dfg_reg_alloc_num_gprs + i);
    }
    return result;
}();

}   // namespace internal

constexpr bool IsRegisterUsedForDfgRegAllocation(X64Reg reg)
{
    if (reg.IsGPR())
    {
        return internal::x_gpr_to_seq_ord_map[reg.MachineOrd()] != static_cast<uint8_t>(-1);
    }
    else
    {
        return internal::x_fpr_to_seq_ord_map[reg.MachineOrd()] != static_cast<uint8_t>(-1);
    }
}

static_assert(!IsRegisterUsedForDfgRegAllocation(x_dfg_custom_purpose_temp_reg));

// Return the ordinal of a register in the sequence of all registers participating in regalloc (GPR come first, then FPR)
// This sequence ordinal is also the index into the register spill area used by this register
//
constexpr size_t GetDfgRegAllocSequenceOrdForReg(X64Reg reg)
{
    if (reg.IsGPR())
    {
        TestAssert(internal::x_gpr_to_seq_ord_map[reg.MachineOrd()] != static_cast<uint8_t>(-1));
        return internal::x_gpr_to_seq_ord_map[reg.MachineOrd()];
    }
    else
    {
        TestAssert(internal::x_fpr_to_seq_ord_map[reg.MachineOrd()] != static_cast<uint8_t>(-1));
        return internal::x_fpr_to_seq_ord_map[reg.MachineOrd()];
    }
}

// Map sequence ordinal above back to register
//
constexpr X64Reg GetDfgRegFromRegAllocSequenceOrd(size_t seqOrd)
{
    if (seqOrd < x_dfg_reg_alloc_num_gprs)
    {
        return x_dfg_reg_alloc_gprs[seqOrd];
    }
    else
    {
        TestAssert(seqOrd < x_dfg_reg_alloc_num_gprs + x_dfg_reg_alloc_num_fprs);
        return x_dfg_reg_alloc_fprs[seqOrd - x_dfg_reg_alloc_num_gprs];
    }
}

constexpr size_t x_dfg_reg_alloc_num_group1_gprs = []() {
    size_t result = 0;
    for (size_t i = 0; i < x_dfg_reg_alloc_num_gprs; i++)
    {
        if (x_dfg_reg_alloc_gprs[i].MachineOrd() < 8)
        {
            result++;
        }
    }
    return result;
}();
